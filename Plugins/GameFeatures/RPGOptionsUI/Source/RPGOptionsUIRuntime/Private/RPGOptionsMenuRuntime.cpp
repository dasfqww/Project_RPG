#include "RPGOptionsMenuRuntime.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "UI/Option/RPGOptionMenu.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"

namespace
{
    bool HasOptionsViewportSettings(UGameViewportClient& Viewport)
    {
        return !Viewport.IgnoreInput()
            && Viewport.GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown
            && Viewport.GetMouseLockMode() == EMouseLockMode::DoNotLock
            && !Viewport.HideCursorDuringCapture();
    }

    TSharedPtr<SWidget> FindRestorableFocus(const TWeakPtr<SWidget>& PreviousFocus)
    {
        TSharedPtr<SWidget> Focus = PreviousFocus.Pin();
        FWidgetPath Path;
        return Focus && Focus->IsEnabled()
            && FSlateApplication::Get().GeneratePathToWidgetUnchecked(Focus.ToSharedRef(), Path)
            ? Focus : nullptr;
    }

    bool IsUsableOptionsFocus(const TSharedPtr<SWidget>& Focus, const TSharedPtr<SWidget>& Root)
    {
        FWidgetPath Path;
        if (!Focus || !Root || !Focus->SupportsKeyboardFocus()
            || !FSlateApplication::Get().GeneratePathToWidgetUnchecked(Focus.ToSharedRef(), Path)
            || !Path.ContainsWidget(Root.Get())) { return false; }
        for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
        {
            const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
            if (!Widget->IsEnabled() || !Widget->GetVisibility().IsVisible()) { return false; }
        }
        return true;
    }

    bool OwnsFocus(const FRPGOptionsMenuInputState& State, const FSlateUser& User,
        const FReply& PendingOperations)
    {
        const TSharedPtr<SWidget> Root = State.Root.Pin();
        const TSharedPtr<SWidget> Applied = State.AppliedFocus.Pin();
        if (!Root) { return false; }
        const TSharedPtr<SWidget> Pending = PendingOperations.GetUserFocusRecepient();
        // A later SetInputMode/SetUserFocus request takes precedence even before Slate processes it.
        if (Pending && Pending != Root && Pending != Applied && !IsUsableOptionsFocus(Pending, Root)) { return false; }
        if (PendingOperations.ShouldReleaseUserFocus()) { return false; }
        if (User.IsWidgetInFocusPath(Root)) { return true; }

        const TSharedPtr<SWidget> Focus = User.GetFocusedWidget();
        const UGameViewportClient* Viewport = State.Viewport.Get();
        if (!Pending && (!Focus || (Viewport && Focus == Viewport->GetGameViewportWidget()))) { return true; }
        return Applied && Pending == Applied && (!Focus || Focus == State.PreviousFocus.Pin()
            || Focus == State.PreviousActualFocus.Pin());
    }

    bool HasAnotherPlayerUI(ULocalPlayer& Owner, UWorld& World, UGameViewportClient& Viewport)
    {
        const UGameInstance* Instance = World.GetGameInstance();
        const TSharedPtr<SWidget> ViewportWidget = Viewport.GetGameViewportWidget();
        if (!Instance) { return false; }
        for (ULocalPlayer* Player : Instance->GetLocalPlayers())
        {
            if (!IsValid(Player) || Player == &Owner || Player->ViewportClient != &Viewport) { continue; }
            const APlayerController* Controller = Player->GetPlayerController(&World);
            const TSharedPtr<const FSlateUser> User = Player->GetSlateUser();
            if (IsValid(Controller) && Controller->bShowMouseCursor && User)
            {
                const TSharedPtr<SWidget> Focus = User->GetFocusedWidget();
                const TSharedPtr<SWidget> Pending = Player->GetSlateOperations().GetUserFocusRecepient();
                if ((Focus && Focus != ViewportWidget) || (Pending && Pending != ViewportWidget))
                {
                    return true;
                }
            }
        }
        return false;
    }

    /** Restore observable settings and the controller's focus-flush policy in every build configuration. */
    struct FRestoredOptionsInputMode final : FInputModeDataBase
    {
        FRestoredOptionsInputMode(const FRPGOptionsMenuInputState& InState, bool bInRestoreViewport)
            : State(InState), bRestoreViewport(bInRestoreViewport) {}

        virtual ~FRestoredOptionsInputMode() override = default;

        virtual void ApplyInputMode(FReply& Operations, UGameViewportClient& Viewport) const override
        {
            const TSharedPtr<SWidget> Focus = FindRestorableFocus(State.PreviousFocus);
            if (Focus) { Operations.SetUserFocus(Focus.ToSharedRef()); }
            else { Operations.ClearUserFocus(); }
            if (!bRestoreViewport || !State.SharedViewport) { return; }

            const FRPGOptionsViewportInputState& Saved = *State.SharedViewport;
            const TSharedPtr<SViewport> ViewportWidget = Viewport.GetGameViewportWidget();
            if (ViewportWidget)
            {
                const bool bPermanentCapture = Saved.CaptureMode == EMouseCaptureMode::CapturePermanently
                    || Saved.CaptureMode == EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown;
                if (bPermanentCapture)
                {
                    Operations.UseHighPrecisionMouseMovement(ViewportWidget.ToSharedRef());
                    if (!Focus) { Operations.SetUserFocus(ViewportWidget.ToSharedRef()); }
                }
                else { Operations.ReleaseMouseCapture(); }
                const bool bLock = Saved.LockMode == EMouseLockMode::LockAlways
                    || (Saved.LockMode == EMouseLockMode::LockOnCapture && bPermanentCapture)
                    || (Saved.LockMode == EMouseLockMode::LockInFullscreen && Viewport.IsExclusiveFullscreenViewport());
                if (bLock) { Operations.LockMouseToWidget(ViewportWidget.ToSharedRef()); }
                else { Operations.ReleaseMouseLock(); }
            }
            Viewport.SetIgnoreInput(Saved.bIgnoreInput);
            Viewport.SetHideCursorDuringCapture(Saved.bHideCursorDuringCapture);
            Viewport.SetMouseCaptureMode(Saved.CaptureMode);
            Viewport.SetMouseLockMode(Saved.LockMode);
        }

        virtual bool ShouldFlushInputOnViewportFocus() const override { return State.bPreviousFlushKeys; }
#if UE_ENABLE_DEBUG_DRAWING
        virtual const FString& GetDebugDisplayName() const override { return State.PreviousDebugName; }
#endif

        FRPGOptionsMenuInputState State;
        bool bRestoreViewport;
    };
}

ERPGOptionsNavigationCommand FRPGOptionsMenuRuntime::GetNavigationCommand(const FKeyEvent& Event) const
{
    const FKey Key = Event.GetKey();
    if (Key == EKeys::Tab && Event.IsControlDown() && !Event.IsAltDown() && !Event.IsCommandDown())
    {
        return Event.IsShiftDown() ? ERPGOptionsNavigationCommand::PreviousTab : ERPGOptionsNavigationCommand::NextTab;
    }
    if (Event.IsControlDown() || Event.IsAltDown() || Event.IsShiftDown() || Event.IsCommandDown())
    {
        return ERPGOptionsNavigationCommand::None;
    }
    if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right) { return ERPGOptionsNavigationCommand::Back; }
    if (Key == EKeys::Gamepad_LeftShoulder) { return ERPGOptionsNavigationCommand::PreviousTab; }
    if (Key == EKeys::Gamepad_RightShoulder) { return ERPGOptionsNavigationCommand::NextTab; }
    return ERPGOptionsNavigationCommand::None;
}

bool FRPGOptionsMenuRuntime::CanNavigate(const URPGOptionMenu* Widget, const FRPGOptionsMenuInputState& State) const
{
    if (!State.bActive || !State.bApplied || !IsValid(Widget) || Widget->IsRemovingFromParent()
        || !Widget->IsInViewport() || !Widget->IsVisible() || !Widget->GetIsEnabled()
        || !FSlateApplication::IsInitialized()) { return false; }
    APlayerController* Controller = State.Controller.Get();
    ULocalPlayer* Player = State.LocalPlayer.Get();
    UWorld* World = State.World.Get();
    UGameViewportClient* Viewport = State.Viewport.Get();
    const TSharedPtr<SWidget> Root = State.Root.Pin();
    if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
        || !IsValid(Player) || Controller->GetLocalPlayer() != Player || Widget->GetOwningLocalPlayer() != Player
        || Widget->GetOwningPlayer() != Controller || !IsValid(World) || World->bIsTearingDown
        || Widget->GetWorld() != World || Controller->GetWorld() != World
        || !IsValid(Viewport) || World->GetGameViewport() != Viewport || Player->ViewportClient != Viewport
        || !Root || Widget->GetCachedWidget() != Root || !Controller->bShowMouseCursor
        || Controller->ShouldFlushKeysWhenViewportFocusChanges() || !HasOptionsViewportSettings(*Viewport))
    {
        return false;
    }
    const TSharedPtr<FSlateUser> User = Player->GetSlateUser();
    return User && User->GetUserIndex() == State.SlateUserIndex && User->IsWidgetInFocusPath(Root)
        && OwnsFocus(State, *User, Player->GetSlateOperations());
}

bool FRPGOptionsMenuRuntime::AcquireInput(URPGOptionMenu* Widget, FRPGOptionsMenuInputState& State)
{
    check(IsInGameThread());
    if (State.bActive) { return true; }
    if (!IsValid(Widget) || Widget->IsRemovingFromParent()
        || !Widget->IsInViewport() || !Widget->IsVisible() || !Widget->GetIsEnabled() || !FSlateApplication::IsInitialized())
    {
        return false;
    }
    APlayerController* Controller = Widget->GetOwningPlayer();
    ULocalPlayer* Player = IsValid(Controller) ? Controller->GetLocalPlayer() : nullptr;
    UWorld* World = Widget->GetWorld();
    UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    const TSharedPtr<SWidget> Root = Widget->GetCachedWidget();
    const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
    if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
        || !IsValid(Player) || !World || World->bIsTearingDown || Controller->GetWorld() != World
        || !IsValid(Viewport) || Player->ViewportClient != Viewport || !Root || !User)
    {
        return false;
    }

    TStrongObjectPtr<URPGOptionMenu> KeepAlive(Widget);
    State = FRPGOptionsMenuInputState();
    State.Controller = Controller;
    State.LocalPlayer = Player;
    State.World = World;
    State.Viewport = Viewport;
    State.Root = Root;
    State.SlateUserIndex = User->GetUserIndex();
    State.bPreviousFocusable = Widget->IsFocusable();
    State.SessionId = FGuid::NewGuid();
    const FGuid SessionId = State.SessionId;
    State.bActive = true;
    Widget->SetIsFocusable(true);

    // This Blueprint focus query may close or remove the widget.
    UWidget* Desired = Widget->GetDesiredFocusTarget();
    if (!State.bActive || State.SessionId != SessionId) { return false; }
    if (!IsValid(Widget) || Widget->IsRemovingFromParent() || !Widget->IsInViewport()
        || !Widget->IsVisible() || !Widget->GetIsEnabled() || World->bIsTearingDown || Controller->IsActorBeingDestroyed()
        || Widget->GetOwningPlayer() != Controller || Controller->GetLocalPlayer() != Player
        || Controller->GetWorld() != World || Player->ViewportClient != Viewport || Player->GetSlateUser() != User)
    {
        ReleaseInput(Widget, State, false);
        return false;
    }
    TSharedPtr<SWidget> Focus = Widget->GetRememberedOptionsFocus();
    if (!IsUsableOptionsFocus(Focus, Root))
    {
        Focus = IsValid(Desired) ? Desired->GetCachedWidget() : nullptr;
    }
    if (!IsUsableOptionsFocus(Focus, Root))
    {
        UWidget* SelectedTab = Widget->GetSelectedTabFocusTarget();
        Focus = IsValid(SelectedTab) ? SelectedTab->GetCachedWidget() : nullptr;
    }
    if (!IsUsableOptionsFocus(Focus, Root)) { Focus = Root; }
    State.AppliedFocus = Focus;

    // Capture after the Blueprint query so its changes become the previous state.
    State.PreviousActualFocus = User->GetFocusedWidget();
    const FReply& PreviousOperations = Player->GetSlateOperations();
    const TSharedPtr<SWidget> PendingFocus = PreviousOperations.GetUserFocusRecepient();
    State.PreviousFocus = PreviousOperations.ShouldReleaseUserFocus() ? nullptr
        : (PendingFocus ? PendingFocus : User->GetFocusedWidget());
    State.bPreviousCursorVisible = Controller->bShowMouseCursor;
    State.bPreviousFlushKeys = Controller->ShouldFlushKeysWhenViewportFocusChanges();
#if UE_ENABLE_DEBUG_DRAWING
    State.PreviousDebugName = Controller->GetCurrentInputModeDebugString();
#endif


    for (auto It = ViewportStates.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid() || !It.Value().IsValid()) { It.RemoveCurrent(); }
    }
    TSharedPtr<FRPGOptionsViewportInputState> Shared = ViewportStates.FindRef(Viewport).Pin();
    if (!Shared)
    {
        Shared = MakeShared<FRPGOptionsViewportInputState>();
        Shared->CaptureMode = Viewport->GetMouseCaptureMode();
        Shared->LockMode = Viewport->GetMouseLockMode();
        Shared->bIgnoreInput = Viewport->IgnoreInput();
        Shared->bHideCursorDuringCapture = Viewport->HideCursorDuringCapture();
        ViewportStates.Add(Viewport, Shared);
    }
    Shared->Widgets.AddUnique(Widget);
    State.SharedViewport = Shared;

    FInputModeGameAndUI InputMode;
    InputMode.SetWidgetToFocus(Focus);
    InputMode.SetHideCursorDuringCapture(false);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    State.bApplied = true;
    Controller->SetInputMode(InputMode);
    Controller->SetShowMouseCursor(true);
    return true;
}

void FRPGOptionsMenuRuntime::ReleaseInput(URPGOptionMenu* Widget, FRPGOptionsMenuInputState& State,
    bool bRestoreInput)
{
    check(IsInGameThread());
    if (!State.bActive) { return; }
    const FRPGOptionsMenuInputState Saved = State;
    State = FRPGOptionsMenuInputState();

    bool bLastViewportWidget = true;
    if (Saved.SharedViewport)
    {
        Saved.SharedViewport->Widgets.RemoveAll([Widget](const TWeakObjectPtr<URPGOptionMenu>& Entry)
        {
            return !Entry.IsValid() || Entry.Get() == Widget;
        });
        bLastViewportWidget = Saved.SharedViewport->Widgets.IsEmpty();
        if (bLastViewportWidget && ViewportStates.FindRef(Saved.Viewport).Pin() == Saved.SharedViewport)
        {
            ViewportStates.Remove(Saved.Viewport);
        }
    }
    if (IsValid(Widget)) { Widget->SetIsFocusable(Saved.bPreviousFocusable); }

    APlayerController* Controller = Saved.Controller.Get();
    ULocalPlayer* Player = Saved.LocalPlayer.Get();
    UGameViewportClient* Viewport = Saved.Viewport.Get();
    UWorld* World = Saved.World.Get();
    if (!bRestoreInput || !Saved.bApplied || !IsValid(Controller) || Controller->IsActorBeingDestroyed()
        || !IsValid(Player) || Controller->GetLocalPlayer() != Player || !World || World->bIsTearingDown
        || Controller->GetWorld() != World || !IsValid(Viewport) || World->GetGameViewport() != Viewport
        || Player->ViewportClient != Viewport || !FSlateApplication::IsInitialized())
    {
        return;
    }
    const TSharedPtr<FSlateUser> User = Player->GetSlateUser();
    if (!User || User->GetUserIndex() != Saved.SlateUserIndex) { return; }
    FReply& Pending = Player->GetSlateOperations();
    const bool bOwnsInput = OwnsFocus(Saved, *User, Pending)
        && Controller->bShowMouseCursor && !Controller->ShouldFlushKeysWhenViewportFocusChanges()
        && HasOptionsViewportSettings(*Viewport);
    if (!bOwnsInput)
    {
        // Remove only our queued focus request; keep a later UI's effective input settings.
        const TSharedPtr<SWidget> QueuedFocus = Pending.GetUserFocusRecepient();
        if (QueuedFocus && (QueuedFocus == Saved.AppliedFocus.Pin() || QueuedFocus == Saved.Root.Pin()))
        {
            const TSharedPtr<SWidget> Focus = User->GetFocusedWidget();
            if (Focus && !User->IsWidgetInFocusPath(Saved.Root.Pin()))
            {
                Pending.SetUserFocus(Focus.ToSharedRef());
            }
            else { Pending.ClearUserFocus(); }
        }
        return;
    }

    const bool bRestoreViewport = bLastViewportWidget && !HasAnotherPlayerUI(*Player, *World, *Viewport);
    Controller->SetInputMode(FRestoredOptionsInputMode(Saved, bRestoreViewport));
    Controller->SetShowMouseCursor(Saved.bPreviousCursorVisible);
}
