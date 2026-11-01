// Development-only routed input for the existing opt-in online flow fixture.
#include "Tests/RPGDungeonFlowE2EProbe.h"

#if !UE_BUILD_SHIPPING
#include "CommonGameViewportClient.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "UI/RPGMenuFocus.h"
#include "Widgets/SWindow.h"
#endif

bool URPGDungeonFlowE2EClient::ClickFlowButton(UButton* Button)
{
#if !UE_BUILD_SHIPPING
	if (!Button || !Button->GetIsEnabled()) return Fail(TEXT("PointerButtonUnavailable"));
	if (!bSlatePointerInput)
	{
		Button->OnClicked.Broadcast();
		return true;
	}
	auto* Viewport = Cast<UCommonGameViewportClient>(GetGameInstance()->GetGameViewportClient());
	auto* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	const auto User = LocalPlayer ? LocalPlayer->GetSlateUser() : nullptr;
	const auto Window = Viewport ? Viewport->GetWindow() : nullptr;
	if (!Viewport || !Window || !User || !FSlateApplication::IsInitialized())
		return Fail(TEXT("PointerCommonViewportUnavailable"));
	auto* Screen = Button->GetTypedOuter<UUserWidget>();
	auto* PC = GetGameInstance()->GetFirstLocalPlayerController();
	auto* Desired = Screen ? Screen->GetDesiredFocusWidget() : nullptr;
	if (!PC || !Screen || !Desired || !Desired->GetIsEnabled() || !Desired->IsVisible()
		|| !Desired->SupportsKeyboardFocus() || Screen->HasUserFocus(PC) || !Screen->HasUserFocusedDescendants(PC))
		return Fail(TEXT("MenuInitialOrLateFocusNotReady"));
	const auto FocusBefore = User->GetFocusedWidget();
	RPGMenuFocus::Update(*Screen, {Desired});
	if (User->GetFocusedWidget() != FocusBefore) return Fail(TEXT("MenuRefreshStoleExistingFocus"));
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_MENU_FOCUS Verified=1 Desired=%s RootFocused=0 ChildFocused=1 ExistingFocusPreserved=1 Physical=0"), *Desired->GetName());

	// Reveal clipped lobby rows without changing their layout or bypassing the
	// hittest grid. No physical wheel, OS cursor positioning or window activation.
	for (auto* Parent = Button->GetParent(); Parent; Parent = Parent->GetParent())
		if (auto* Scroll = Cast<UScrollBox>(Parent))
		{
			Scroll->EndInertialScrolling();
			Scroll->ScrollWidgetIntoView(Button, false, EDescendantScrollDestination::Center);
		}
	auto& Slate = FSlateApplication::Get();
	if (Slate.IsTicking()) return Fail(TEXT("PointerProbeCannotReenterSlateTick"));
	// The probe runs on the core ticker, not during Slate event dispatch. Paint
	// pending confirmation/scroll changes before using the real window's grid.
	Slate.Tick(ESlateTickType::Widgets);
	Slate.Tick(ESlateTickType::Widgets);
	const auto Target = Button->GetCachedWidget();
	if (!Target || Target->GetCachedGeometry().GetLocalSize().IsNearlyZero())
		return Fail(TEXT("PointerButtonHasNoGeometry"));
	const FVector2D Position = Target->GetCachedGeometry().LocalToAbsolute(Target->GetCachedGeometry().GetLocalSize() * .5f);
	auto HitWidgets = Window->GetHittestGrid().GetBubblePath(Position, 0.f, false, User->GetUserIndex());
	const FWidgetPath Path(HitWidgets);
	if (!Path.IsValid() || !Path.ContainsWidget(Target.Get()))
		return Fail(TEXT("PointerButtonOccludedOrClipped"));

	const TSet<FKey> Released;
	const FPointerEvent Move(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
		Position, Position, Released, FKey(), 0.f, FModifierKeysState());
	Slate.RoutePointerMoveEvent(Path, Move, false);
	const TSet<FKey> Pressed{EKeys::LeftMouseButton};
	const FPointerEvent Down(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
		Position, Position, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	const FPointerEvent Up(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
		Position, Position, Released, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	const bool bDownHandled = Slate.RoutePointerDownEvent(Path, Down).IsEventHandled();
	const bool bUpHandled = Slate.RoutePointerUpEvent(Path, Up).IsEventHandled();
	if (!bDownHandled || !bUpHandled) return Fail(TEXT("PointerButtonDidNotHandleClick"));
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_POINTER CLICK Widget=%s HitTest=1 CommonViewport=1 Physical=0"), *Button->GetName());
	return true;
#else
	return false;
#endif
}

bool URPGDungeonFlowE2EClient::VerifyFlowConfirmationNavigation(UButton* Open, UButton* Confirm, UButton* Cancel)
{
#if !UE_BUILD_SHIPPING
	if (!bConfirmationNavigation) return true;
	auto* PC = GetGameInstance()->GetFirstLocalPlayerController();
	auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	if (!PC || !Flow || !Open || !Confirm || !Cancel || !Cancel->HasUserFocus(PC))
		return Fail(TEXT("ConfirmationMustInitiallyFocusCancel"));
	const auto Session = Flow->GetCurrentDungeonSession().DungeonSessionId;
	const auto Character = Flow->GetSelectedCharacterId();
	const auto FlowState = Flow->GetFlowState();
	const bool bUnresolved = Flow->HasUnresolvedDungeonAbandonment();
	const bool bQueued = Flow->HasQueuedDungeonAbandonment();
	const bool bInFlight = Flow->IsRequestInFlight();
	auto* LocalPlayer = PC->GetLocalPlayer();
	const auto User = LocalPlayer ? LocalPlayer->GetSlateUser() : nullptr;
	if (!User || !FSlateApplication::IsInitialized()) return Fail(TEXT("ConfirmationSlateUserMissing"));
	auto& Slate = FSlateApplication::Get();
	auto Send = [&](FKey Key, bool bShift = false, bool bRepeat = false)
	{
		const FModifierKeysState Modifiers(bShift, false, false, false, false, false, false, false, false);
		const bool bHandled = Slate.ProcessKeyDownEvent(FKeyEvent(Key, Modifiers, User->GetUserIndex(), bRepeat, 0, 0));
		Slate.ProcessKeyUpEvent(FKeyEvent(Key, Modifiers, User->GetUserIndex(), false, 0, 0));
		return bHandled;
	};
	// No test-side focus assignment: start from the production Open callback.
	if (!Send(EKeys::Tab) || !Confirm->HasUserFocus(PC)
		|| !Send(EKeys::Tab, true) || !Cancel->HasUserFocus(PC)
		|| !Send(EKeys::Gamepad_DPad_Down) || !Confirm->HasUserFocus(PC)
		|| !Send(EKeys::Gamepad_DPad_Up) || !Cancel->HasUserFocus(PC))
		return Fail(TEXT("ConfirmationNavigationEscapedChoices"));
	const bool bLobby = GetGameInstance()->GetWorld()->GetNetMode() == NM_Standalone;
	const FKey Back = bLobby ? EKeys::Virtual_Gamepad_Back.GetVirtualKey() : EKeys::Escape;
	if (!Send(Back, false, true) || !Cancel->GetIsEnabled() || !Cancel->HasUserFocus(PC))
		return Fail(TEXT("ConfirmationHeldBackDismissedConsent"));
	if (!Send(Back) || Confirm->GetIsEnabled() || Cancel->GetIsEnabled() || !Open->HasUserFocus(PC)
		|| Flow->GetCurrentDungeonSession().DungeonSessionId != Session || Flow->GetSelectedCharacterId() != Character
		|| Flow->GetFlowState() != FlowState || Flow->HasUnresolvedDungeonAbandonment() != bUnresolved
		|| Flow->HasQueuedDungeonAbandonment() != bQueued || Flow->IsRequestInFlight() != bInFlight)
		return Fail(TEXT("ConfirmationBackMutatedSessionOrLostFocus"));
	if (!ClickFlowButton(Open) || !Cancel->HasUserFocus(PC) || !Confirm->GetIsEnabled())
		return Fail(TEXT("ConfirmationReopenDidNotRestoreSafeFocus"));
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_FLOW_CONFIRMATION_NAV Verified=1 Lobby=%d SafeInitialFocus=1 TabShiftTab=1 GamepadDirections=1 BackCancel=1 HeldBackConsumed=1 IdentityRetained=1 Physical=0"), bLobby ? 1 : 0);
	return true;
#else
	return false;
#endif
}
