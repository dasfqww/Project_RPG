#include "UI/RPGWindowInputState.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SOverlay.h"

bool FRPGWindowInputState::ContainsWindow(const UUserWidget& Window) const
{
	return Windows.ContainsByPredicate([&Window](const FWindowEntry& Entry) { return Entry.Window.Get() == &Window; });
}

UUserWidget* FRPGWindowInputState::GetTopWindow() const
{
	UUserWidget* Top = nullptr;
	int32 TopLayer = MIN_int32;
	for (const FWindowEntry& Entry : Windows)
	{
		if (Entry.Window.IsValid() && Entry.Layer >= TopLayer)
		{
			Top = Entry.Window.Get();
			TopLayer = Entry.Layer;
		}
	}
	return Top;
}

void FRPGWindowInputState::RefreshZOrder()
{
	UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get();
	if (!Viewport) return;
	TMap<int32, int32> LayerOffsets;
	for (const FWindowEntry& Entry : Windows)
	{
		UUserWidget* Window = Entry.Window.Get();
		if (!Window) continue;
		const int32 ZOrder = Entry.Layer + LayerOffsets.FindOrAdd(Entry.Layer)++;
		auto Slot = Viewport->GetWidgetSlot(Window);
		Slot.ZOrder = ZOrder;
		Viewport->SetWidgetSlot(Window, Slot);

		// UE 5.8 SetWidgetSlot only changes the inner canvas, NOT the viewport's
		// overlay layer. Update that exact host slot in place: removing/re-adding
		// the UUserWidget would destruct controls and interrupt mouse capture.
		const TSharedPtr<SWidget> Content = Window->GetCachedWidget();
		const TSharedPtr<SWidget> Canvas = Content ? Content->GetParentWidget() : nullptr;
		const TSharedPtr<SWidget> Overlay = Canvas ? Canvas->GetParentWidget() : nullptr;
		if (!Canvas || Canvas->GetType() != FName(TEXT("SConstraintCanvas"))
			|| !Overlay || Overlay->GetType() != FName(TEXT("SOverlay"))) continue;
		FChildren* Children = Overlay->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (Children->GetChildAt(Index) == Canvas)
			{
				auto& OverlaySlot = const_cast<SOverlay::FOverlaySlot&>(
					static_cast<const SOverlay::FOverlaySlot&>(Children->GetSlotAt(Index)));
				// AddToViewport reserves the first ten layers for built-in controls.
				OverlaySlot.SetZOrder(ZOrder + 10);
				break;
			}
		}
	}
}

bool FRPGWindowInputState::BeginWindow(APlayerController& Controller, UUserWidget& Window, const int32 Layer)
{
	if (bInputLocksApplied && Owner.Get() != &Controller) return false;
	if (ContainsWindow(Window)) return true;
	const bool bFirstWindow = !bInputLocksApplied;
	if (!bInputLocksApplied)
	{
		Owner = &Controller;
		bPreviousCursorVisible = Controller.bShowMouseCursor;
		PreviousFocus.Reset();
		if (FSlateApplication::IsInitialized() && Controller.GetLocalPlayer())
		{
			const int32 UserIndex = FSlateApplication::Get().GetUserIndexForController(
				Controller.GetLocalPlayer()->GetControllerId());
			PreviousFocus = FSlateApplication::Get().GetUserFocusedWidget(UserIndex);
		}
		// Controller locks are counted. Never reset locks owned by another system.
		Controller.SetIgnoreMoveInput(true);
		Controller.SetIgnoreLookInput(true);
		bInputLocksApplied = true;
	}
	Windows.Add({&Window, Layer});
	Controller.SetShowMouseCursor(true);
	if (bFirstWindow) OnFirstWindowOpened.Broadcast();
	return ContainsWindow(Window);
}

void FRPGWindowInputState::FocusWindow(UUserWidget& Window)
{
	APlayerController* Controller = Owner.Get();
	if (!Controller || !ContainsWindow(Window)) return;
	const int32 Index = Windows.IndexOfByPredicate(
		[&Window](const FWindowEntry& Entry) { return Entry.Window.Get() == &Window; });
	const FWindowEntry Entry = Windows[Index];
	Windows.RemoveAt(Index);
	Windows.Add(Entry);
	// A normal window never takes keyboard focus away from a higher modal layer.
	UUserWidget* Top = GetTopWindow();
	if (!Top) return;
	// TakeWidget may construct a widget and synchronously close it.
	const TSharedRef<SWidget> Focus = Top->TakeWidget();
	if (GetTopWindow() != Top || Owner.Get() != Controller) return;
	RefreshZOrder();
	Controller->SetShowMouseCursor(true);
	if (Controller->GetLocalPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Focus);
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Controller->SetInputMode(Mode);
	}
}

void FRPGWindowInputState::EndWindow(UUserWidget& Window)
{
	if (Windows.RemoveAll([&Window](const FWindowEntry& Entry) { return Entry.Window.Get() == &Window; }) == 0) return;
	Windows.RemoveAll([](const FWindowEntry& Entry) { return !Entry.Window.IsValid(); });
	if (Windows.IsEmpty()) Reset();
	else FocusWindow(*GetTopWindow());
}

void FRPGWindowInputState::Reset()
{
	APlayerController* Controller = Owner.Get();
	const bool bRestore = bInputLocksApplied;
	const bool bCursor = bPreviousCursorVisible;
	const TSharedPtr<SWidget> Focus = PreviousFocus.Pin();
	// Clear our state before focus callbacks can open or close another window.
	Windows.Reset();
	Owner.Reset();
	PreviousFocus.Reset();
	bInputLocksApplied = false;
	if (!Controller || !bRestore) return;
	Controller->SetIgnoreMoveInput(false);
	Controller->SetIgnoreLookInput(false);
	Controller->SetShowMouseCursor(bCursor);
	if (!Controller->GetLocalPlayer()) return;
	if (bCursor)
	{
		FInputModeGameAndUI Mode;
		if (Focus) Mode.SetWidgetToFocus(Focus);
		Controller->SetInputMode(Mode);
	}
	else Controller->SetInputMode(FInputModeGameOnly());
}
