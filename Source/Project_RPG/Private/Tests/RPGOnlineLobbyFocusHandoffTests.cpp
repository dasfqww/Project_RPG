#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/RPGMenuFocus.h"
#include "Blueprint/WidgetTree.h"
#include "GenericPlatform/GenericWindow.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SVirtualWindow.h"

namespace
{
	// SVirtualWindow supplies an inert FGenericWindow (no OS handle). Only the
	// regular-window classification changes so synthetic activation can exercise
	// the real production top-level-window guard, without displaying a window.
	class SFocusFixtureWindow : public SVirtualWindow
	{
	public:
		void Construct(const FArguments& InArgs)
		{
			SVirtualWindow::Construct(InArgs);
			bIsPopupWindow = false;
			SetIsFocusable(true);
		}
	};

	struct FFocusViewportFixture
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
		UWorld* World = nullptr;
		UGameViewportClient* Viewport = nullptr;
		ULocalPlayer* Player = nullptr;
		APlayerController* Controller = nullptr;
		URPGOnlineLobbyWidget* Widget = nullptr;
		TSharedPtr<SWindow> Window;
		TSharedPtr<SWindow> OtherWindow;
		TSharedPtr<SOverlay> Overlay;
		TSharedPtr<SWidget> OtherButton;
		TSharedPtr<SWindow> PreviousWindow;
		TSharedPtr<SWidget> PreviousFocus;
		bool bPreviousActive = false;
		bool bInstanceInitialized = false;
		int32 UserIndex = INDEX_NONE;
		FString Failure;
		FFocusViewportFixture()
		{
			if (!FSlateApplication::IsInitialized()) { Failure = TEXT("SlateNotInitialized"); return; }
			auto& Application = FSlateApplication::Get();
			bPreviousActive = Application.IsActive(); PreviousWindow = Application.GetActiveTopLevelWindow();
			Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/LobbyFocusViewport_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			bInstanceInitialized = true;
			World = Instance->GetWorld();
			if (!World) { Failure = TEXT("MissingWorld"); return; }
			Viewport = NewObject<UGameViewportClient>(GEngine);
			Instance->GetWorldContext()->GameViewport = Viewport;
			Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
			Overlay = SNew(SOverlay);
			Window = SNew(SFocusFixtureWindow).Size(FVector2D(1280, 720));
			Window->SetContent(Overlay.ToSharedRef());
			Application.RegisterVirtualWindow(Window.ToSharedRef());
			Viewport->SetViewportOverlayWidget(Window, Overlay.ToSharedRef());
			FString PlayerError;
			Player = Instance->CreateLocalPlayer(0, PlayerError, false);
			if (!Player) { Failure = TEXT("MissingLocalPlayer"); return; }
			const auto User = Player->GetSlateUser();
			if (!User) { Failure = TEXT("MissingSlateUser"); return; }
			UserIndex = User->GetUserIndex(); PreviousFocus = Application.GetUserFocusedWidget(UserIndex);
			Controller = World->SpawnActor<APlayerController>();
			if (!Controller) { Failure = TEXT("MissingController"); return; }
			Controller->SetPlayer(Player);
			// Minimal RPC worlds have no actor initialization/BeginPlay. Register
			// this owned controller through the engine API used by that lifecycle,
			// so FLocalPlayerContext resolves it in the fixture's actual world.
			World->AddController(Controller);
			if (Player->GetPlayerController(World) != Controller) { Failure = TEXT("MissingWorldController"); return; }
			Widget = CreateWidget<URPGOnlineLobbyWidget>(Controller, URPGOnlineLobbyWidget::StaticClass());
			if (!Widget) { Failure = TEXT("MissingLobbyWidget"); return; }
			Widget->AddToViewport(120);
			OtherButton = SNew(SButton);
			OtherWindow = SNew(SFocusFixtureWindow).Size(FVector2D(400, 200));
			OtherWindow->SetContent(OtherButton.ToSharedRef());
			Application.RegisterVirtualWindow(OtherWindow.ToSharedRef());
			Window->SlatePrepass(); OtherWindow->SlatePrepass();
			Activate(Window);
		}
		~FFocusViewportFixture()
		{
			if (Widget) Widget->RemoveFromParent();
			if (FSlateApplication::IsInitialized())
			{
				auto& Application = FSlateApplication::Get();
				if (UserIndex != INDEX_NONE) Application.ClearUserFocus(UserIndex, EFocusCause::SetDirectly);
				if (OtherWindow) { Application.ProcessWindowActivatedEvent(FWindowActivateEvent(FWindowActivateEvent::EA_Deactivate, OtherWindow.ToSharedRef())); Application.UnregisterVirtualWindow(OtherWindow.ToSharedRef()); }
				if (Window) { Application.ProcessWindowActivatedEvent(FWindowActivateEvent(FWindowActivateEvent::EA_Deactivate, Window.ToSharedRef())); Application.UnregisterVirtualWindow(Window.ToSharedRef()); }
				if (PreviousWindow) Application.ProcessWindowActivatedEvent(FWindowActivateEvent(FWindowActivateEvent::EA_Activate, PreviousWindow.ToSharedRef()));
				Application.ProcessApplicationActivationEvent(bPreviousActive);
				if (PreviousFocus && UserIndex != INDEX_NONE) Application.SetUserFocus(UserIndex, PreviousFocus, EFocusCause::SetDirectly);
			}
			if (bInstanceInitialized) Instance->Shutdown();
			if (Viewport) Viewport->DetachViewportClient();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		}
		void Activate(const TSharedPtr<SWindow>& Target)
		{
			auto& Application = FSlateApplication::Get();
			Application.ProcessApplicationActivationEvent(true);
			Application.ProcessWindowActivatedEvent(FWindowActivateEvent(FWindowActivateEvent::EA_Activate, Target.ToSharedRef()));
		}
		bool Focus(UWidget* Target)
		{
			return Target && FSlateApplication::Get().SetUserFocus(UserIndex, Target->GetCachedWidget(), EFocusCause::SetDirectly);
		}
		bool IsFocused(UWidget* Target) const
		{ return Target && FSlateApplication::Get().GetUserFocusedWidget(UserIndex) == Target->GetCachedWidget(); }
		static void Enable(UWidget* Target)
		{
			Target->SetVisibility(ESlateVisibility::Visible); Target->SetIsEnabled(true);
			for (auto* Parent = Target->GetParent(); Parent; Parent = Parent->GetParent())
			{ Parent->SetVisibility(ESlateVisibility::Visible); Parent->SetIsEnabled(true); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyFocusHandoffTest,
	"ProjectRPG.Online.DungeonLobbyFocusHandoff.LocalViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyFocusHandoffTest::RunTest(const FString&)
{
	FFocusViewportFixture F;
	if (!TestTrue(TEXT("Owned viewport fixture ready: ") + F.Failure, F.Failure.IsEmpty())) return false;
	if (!TestTrue(TEXT("The controller is local"), F.Controller->IsLocalController())
		|| !TestTrue(TEXT("The widget resolves its local controller in the owned world"), F.Widget->GetOwningPlayer() == F.Controller)
		|| !TestTrue(TEXT("The lobby was added through the actual viewport subsystem"), F.Widget->IsInViewport())) return false;
	if (!TestTrue(TEXT("Both regular virtual windows have inert native stubs and no OS window handle"),
		F.Window->IsVirtualWindow() && F.OtherWindow->IsVirtualWindow()
		&& F.Window->IsRegularWindow() && F.OtherWindow->IsRegularWindow()
		&& F.Window->GetNativeWindow() && !F.Window->GetNativeWindow()->GetOSWindowHandle()
		&& F.OtherWindow->GetNativeWindow() && !F.OtherWindow->GetNativeWindow()->GetOSWindowHandle())) return false;
	auto& Application = FSlateApplication::Get();
	if (!TestTrue(TEXT("Synthetic application/window activation reaches the production guard"), Application.IsActive()
		&& Application.GetActiveTopLevelWindow() == F.Window)) return false;
	auto* Source = F.Widget->GetWidgetFromName(TEXT("ReloadCharactersButton"));
	auto* Preferred = F.Widget->GetWidgetFromName(TEXT("LoginSteamButton"));
	auto* Input = Cast<UEditableTextBox>(F.Widget->GetWidgetFromName(TEXT("NewCharacterName")));
	if (!TestNotNull(TEXT("Source control"), Source) || !TestNotNull(TEXT("Safe preference"), Preferred)
		|| !TestNotNull(TEXT("Text input"), Input)) return false;
	FFocusViewportFixture::Enable(Source); FFocusViewportFixture::Enable(Preferred); FFocusViewportFixture::Enable(Input);
	F.Widget->SetDesiredFocusWidget(Preferred);
	if (!TestTrue(TEXT("Real Slate user focus starts on the source"), F.Focus(Source) && F.IsFocused(Source))) return false;
	Source->SetIsEnabled(false);
	TestTrue(TEXT("Disabled source still owns focus before production recovery"), F.IsFocused(Source));
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("Production SetUserFocus moves disabled source to the safe preferred control"), F.IsFocused(Preferred));
	Source->SetIsEnabled(true); TestTrue(TEXT("Source can regain focus"), F.Focus(Source));
	Source->SetVisibility(ESlateVisibility::Collapsed);
	TestTrue(TEXT("Hidden source still owns focus before production recovery"), F.IsFocused(Source));
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("A hidden source is repaired through its cached Slate ownership"), F.IsFocused(Preferred));
	Source->SetVisibility(ESlateVisibility::Visible); F.Focus(Source); Source->SetIsEnabled(false);
	F.Widget->SetDesiredFocusWidget(NAME_None); Preferred->SetIsEnabled(false); Input->SetIsEnabled(false);
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("Busy/no preference moves actual focus to the inert screen root"), F.IsFocused(F.Widget));
	Preferred->SetIsEnabled(true);
	RPGMenuFocus::Update(*F.Widget, {Preferred});
	TestTrue(TEXT("The existing root-focus policy resumes to the later enabled safe control"), F.IsFocused(Preferred));
	Input->SetIsEnabled(true);
	TestTrue(TEXT("The available text editor receives focus"), F.Focus(Input));
	const auto TextFocus = Application.GetUserFocusedWidget(F.UserIndex);
	TestTrue(TEXT("Text focus exists before the preservation check"), TextFocus.IsValid());
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("Healthy text-editor focus is not replaced"), Application.GetUserFocusedWidget(F.UserIndex) == TextFocus);
	F.Activate(F.OtherWindow);
	TestTrue(TEXT("Other virtual window receives real Slate focus"), Application.SetUserFocus(F.UserIndex, F.OtherButton, EFocusCause::SetDirectly));
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("Another active window's actual focus is untouched"), Application.GetUserFocusedWidget(F.UserIndex) == F.OtherButton);
	Source->SetIsEnabled(true);
	TestTrue(TEXT("The game control receives focus before switching the active window"), F.Focus(Source));
	Source->SetIsEnabled(false);
	// SetUserFocus itself updates Slate's active top-level window. Simulate the
	// subsequent activation event that retains existing keyboard focus; no mouse
	// input/click or native OS activation is dispatched by this fixture.
	Application.ProcessWindowActivatedEvent(FWindowActivateEvent(FWindowActivateEvent::EA_ActivateByMouse,
		F.OtherWindow.ToSharedRef()));
	TestTrue(TEXT("Foreign-window guard has a stale game control and another active top-level window"),
		F.IsFocused(Source) && Application.GetActiveTopLevelWindow() == F.OtherWindow);
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("Another active window prevents recovery even for an owned stale control"), F.IsFocused(Source));
	F.Activate(F.Window); Source->SetIsEnabled(true); F.Focus(Source); Source->SetIsEnabled(false);
	Application.ProcessApplicationActivationEvent(false);
	const auto InactiveFocus = Application.GetUserFocusedWidget(F.UserIndex);
	TestTrue(TEXT("Inactive-app guard starts with the owned stale focus"), !Application.IsActive() && F.IsFocused(Source));
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("Inactive application does not restore lobby focus"), Application.GetUserFocusedWidget(F.UserIndex) == InactiveFocus);
	F.Activate(F.Window);
	auto* Confirm = F.Widget->GetWidgetFromName(TEXT("ConfirmDungeonAbandonmentButton"));
	auto* Cancel = F.Widget->GetWidgetFromName(TEXT("CancelDungeonAbandonmentButton"));
	if (!TestNotNull(TEXT("Confirm control"), Confirm) || !TestNotNull(TEXT("Cancel control"), Cancel)) return false;
	FFocusViewportFixture::Enable(Confirm); FFocusViewportFixture::Enable(Cancel);
	F.Widget->bAbandonmentConfirmationOpen = true; F.Widget->SetDesiredFocusWidget(Cancel);
	TestTrue(TEXT("Explicit focus fixture starts on confirm without clicking it"), F.Focus(Confirm));
	Confirm->SetIsEnabled(false);
	F.Widget->RefreshInputNavigation();
	TestTrue(TEXT("Disabled confirm hands actual focus to cancel, without dispatching consent"), F.IsFocused(Cancel));
	TestTrue(TEXT("Navigation/focus did not close or submit the confirmation"), F.Widget->bAbandonmentConfirmationOpen);
	F.Widget->RemoveFromParent();
	const auto RemovedFocus = Application.GetUserFocusedWidget(F.UserIndex);
	F.Widget->RefreshInputNavigation();
	TestFalse(TEXT("The removed widget is no longer in a viewport"), F.Widget->IsInViewport());
	TestTrue(TEXT("Removed viewport UI does not reclaim focus"), Application.GetUserFocusedWidget(F.UserIndex) == RemovedFocus);
	return true;
}
#endif
