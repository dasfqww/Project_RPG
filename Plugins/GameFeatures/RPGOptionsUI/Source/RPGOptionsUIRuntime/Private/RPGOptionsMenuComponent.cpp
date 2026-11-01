#include "RPGOptionsMenuComponent.h"

#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Option/RPGOptionMenu.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGOptionsUI, Log, All);

namespace
{
    void ReleaseOptionsInputComponent(UEnhancedInputComponent* Input, APlayerController* Controller)
    {
        if (!Input) { return; }
        if (Controller) { Controller->PopInputComponent(Input); }
        if (!Input->IsBeingDestroyed()) { Input->DestroyComponent(); }
    }
}

URPGOptionsMenuComponent::URPGOptionsMenuComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool URPGOptionsMenuComponent::CanManageOptions(const UWorld* World) const
{
    if (!IsInGameThread() || FeatureReferences <= 0 || bEndingOptions || bUnregisteringOptions
        || !IsRegistered() || !HasBegunPlay() || IsBeingDestroyed() || IsUnreachable()
        || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return false; }
    const ARPGHUD* HUD = Cast<ARPGHUD>(GetOwner());
    return IsValid(HUD) && !HUD->IsActorBeingDestroyed() && IsValid(World) && !World->IsUnreachable()
        && !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        && GetWorld() == World && HUD->GetWorld() == World && World->IsGameWorld()
        && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer;
}

bool URPGOptionsMenuComponent::HasActiveOptionsSession() const
{
    const ARPGHUD* HUD = OptionsHUD.Get();
    const UWorld* World = OptionsWorld.Get();
    const APlayerController* Controller = HUD ? HUD->GetOwningPlayerController() : nullptr;
    const ULocalPlayer* Player = IsValid(Controller) ? Controller->GetLocalPlayer() : nullptr;
    return CanManageOptions(World) && !bReleasingOptions && HUD == GetOwner()
        && IsValid(Controller) && !Controller->IsActorBeingDestroyed()
        && Controller->IsLocalController() && Controller->GetWorld() == World
        && IsValid(Player) && Player->GetPlayerController(World) == Controller
        && IsValid(OptionsWidget) && OptionsWidget->GetOwningPlayer() == Controller
        && !OptionsWidget->IsRemovingFromParent() && HUD->IsOptionMenuProvider(this, OptionsWidget);
}

bool URPGOptionsMenuComponent::HasShortcutBindings() const
{
    return HasActiveOptionsSession() && IsValid(ShortcutInput) && !ShortcutInput->IsBeingDestroyed()
        && ShortcutInput->IsRegistered() && !ShortcutBindingHandles.IsEmpty()
        && ShortcutController.Get() == OptionsHUD->GetOwningPlayerController()
        && ShortcutController->IsInputComponentInStack(ShortcutInput.Get());
}

TArray<FKey> URPGOptionsMenuComponent::GetShortcutKeys() const
{
    TArray<FKey> Keys;
    if (bAcquiringOptions || bUpdatingShortcuts || !HasShortcutBindings()) { return Keys; }
    const APlayerController* Controller = ShortcutController.Get();
    const ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    const UWorld* World = OptionsWorld.Get();
    if (!IsValid(Player) || Player->GetPlayerController(World) != Controller) { return Keys; }
    const UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
    if (!IsValid(Input)) { return Keys; }

    TSet<const UInputAction*> Actions;
    for (const auto& Pair : ToggleActions)
    {
        if (Pair.Value > 0 && IsValid(Pair.Key.Get())) { Actions.Add(Pair.Key.Get()); }
    }
    // This view is borrowed for the synchronous read; never cache mappings or their trigger/modifier objects.
    for (const FEnhancedActionKeyMapping& Mapping : Input->GetEnhancedActionMappingsView())
    {
        if (!Actions.Contains(Mapping.Action.Get())) { continue; }
        const FKey Key = Mapping.Key.GetVirtualKey();
        if (Key.IsValid() && !Key.IsAnalog()) { Keys.AddUnique(Key); }
    }
    Keys.Sort([](const FKey& Left, const FKey& Right)
    {
        if (Left.IsGamepadKey() != Right.IsGamepadKey()) { return !Left.IsGamepadKey(); }
        return Left.GetFName().LexicalLess(Right.GetFName());
    });
    return Keys;
}

void URPGOptionsMenuComponent::CancelPendingRefresh()
{
    UWorld* World = PendingRefreshWorld.Get();
    FTimerHandle Handle = PendingRefreshHandle;
    PendingRefreshHandle.Invalidate();
    PendingRefreshWorld.Reset();
    if (IsValid(World) && !World->IsUnreachable() && Handle.IsValid())
    {
        World->GetTimerManager().ClearTimer(Handle);
    }
}

void URPGOptionsMenuComponent::QueueOptionsRefresh()
{
    UWorld* World = GetWorld();
    if (PendingRefreshHandle.IsValid() || !CanManageOptions(World)) { return; }
    const uint64 Generation = OptionsGeneration;
    const TWeakObjectPtr<UWorld> WeakWorld(World);
    PendingRefreshWorld = World;
    PendingRefreshHandle = World->GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateWeakLambda(this, [this, WeakWorld, Generation]()
        {
            if (Generation != OptionsGeneration || PendingRefreshWorld != WeakWorld) { return; }
            PendingRefreshHandle.Invalidate();
            PendingRefreshWorld.Reset();
            UWorld* OwnerWorld = WeakWorld.Get();
            if (OwnerWorld && GetWorld() == OwnerWorld && !OwnerWorld->bIsTearingDown) { RefreshOptions(); }
        }));
}

void URPGOptionsMenuComponent::AddFeatureReference(bool bCreatedByFeature, UInputAction* ToggleAction)
{
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(this);
    ++FeatureReferences;
    bDestroyWhenUnused |= bCreatedByFeature;
    if (ToggleAction) { ++ToggleActions.FindOrAdd(ToggleAction); }
    RefreshOptions();
}

void URPGOptionsMenuComponent::RemoveFeatureReference(UInputAction* ToggleAction)
{
    check(FeatureReferences > 0);
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(this);
    if (ToggleAction)
    {
        int32* Count = ToggleActions.Find(ToggleAction);
        check(Count && *Count > 0);
        if (--(*Count) == 0) { ToggleActions.Remove(ToggleAction); }
    }
    --FeatureReferences;
    if (FeatureReferences == 0)
    {
        ReleaseOptions(true);
        // Release callbacks can add a new activation to this same component.
        if (FeatureReferences == 0 && bDestroyWhenUnused && !IsBeingDestroyed()) { DestroyComponent(); }
    }
    else { RefreshOptions(); }
}

void URPGOptionsMenuComponent::OnRegister()
{
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(this);
    CancelPendingRefresh();
    ++OptionsGeneration;
    Super::OnRegister();
    // Widget callbacks can deactivate the feature; run them after the engine finishes registration.
    QueueOptionsRefresh();
}

void URPGOptionsMenuComponent::OnUnregister()
{
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(
        !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
    TGuardValue<bool> UnregisteringGuard(bUnregisteringOptions, true);
    CancelPendingRefresh();
    ++OptionsGeneration;
    // A provider-release callback may destroy this component. Mark it unregistered first.
    Super::OnUnregister();
    ReleaseOptions(true);
}

void URPGOptionsMenuComponent::BeginPlay()
{
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(this);
    bEndingOptions = false;
    Super::BeginPlay();
    RefreshOptions();
}

void URPGOptionsMenuComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(
        !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
    bEndingOptions = true;
    CancelPendingRefresh();
    ++OptionsGeneration;
    ReleaseOptions(Reason == EEndPlayReason::Destroyed);
    Super::EndPlay(Reason);
}

void URPGOptionsMenuComponent::RefreshOptions()
{
    if (!CanManageOptions(GetWorld())) { return; }
    if (bAcquiringOptions || bReleasingOptions || bUpdatingShortcuts)
    {
        QueueOptionsRefresh();
        return;
    }
    CancelPendingRefresh();
    if (HasActiveOptionsSession()) { UpdateInputBindings(); }
    else { AcquireOptions(); }
}

void URPGOptionsMenuComponent::AcquireOptions()
{
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(this);
    TGuardValue<bool> AcquiringGuard(bAcquiringOptions, true);
    if (OptionsWidget || OptionsHUD.IsValid()) { ReleaseOptions(false); }
    CancelPendingRefresh();
    if (!CanManageOptions(GetWorld())) { return; }
    TStrongObjectPtr<ARPGHUD> HUD(Cast<ARPGHUD>(GetOwner()));
    TStrongObjectPtr<UWorld> World(GetWorld());
    APlayerController* Candidate = HUD->GetOwningPlayerController();
    TStrongObjectPtr<APlayerController> Controller(IsValid(Candidate) && !Candidate->IsUnreachable() ? Candidate : nullptr);
    if (Controller.IsValid() && !Controller->IsLocalController()) { return; }
    if (!Controller.IsValid() || Controller->IsActorBeingDestroyed())
    {
        QueueOptionsRefresh();
        return;
    }
    ULocalPlayer* CandidatePlayer = Controller->GetLocalPlayer();
    TStrongObjectPtr<ULocalPlayer> Player(IsValid(CandidatePlayer) && !CandidatePlayer->IsUnreachable() ? CandidatePlayer : nullptr);
    if (!Player.IsValid() || Controller->GetWorld() != World.Get()
        || Player->GetPlayerController(World.Get()) != Controller.Get())
    {
        QueueOptionsRefresh();
        return;
    }
    const uint64 Generation = ++OptionsGeneration;
    TStrongObjectPtr<URPGOptionMenu> Widget(HUD->GetOptionMenuWidget());
    const bool bReuseWidget = Widget.IsValid() && Widget->GetOwningPlayer() == Controller.Get()
        && !Widget->IsRemovingFromParent();
    if (!bReuseWidget)
    {
        if (!HUD->OptionMenuWidgetClass)
        {
            UE_LOG(LogRPGOptionsUI, Verbose, TEXT("RPG_OPTIONS SKIPPED HUD=%s Reason=NoWidgetClass"), *HUD->GetName());
            return;
        }
        Widget.Reset(CreateWidget<URPGOptionMenu>(Controller.Get(), HUD->OptionMenuWidgetClass));
    }
    if (!Widget.IsValid()) { return; }
    const auto IsCurrentAttempt = [&]()
    {
        return Generation == OptionsGeneration && CanManageOptions(World.Get()) && !bReleasingOptions
            && GetOwner() == HUD.Get() && HUD->GetOwningPlayerController() == Controller.Get()
            && IsValid(Controller.Get()) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalController()
            && Controller->GetWorld() == World.Get() && IsValid(Player.Get())
            && Controller->GetLocalPlayer() == Player.Get() && Player->GetPlayerController(World.Get()) == Controller.Get()
            && IsValid(Widget.Get()) && Widget->GetOwningPlayer() == Controller.Get() && !Widget->IsRemovingFromParent();
    };
    const auto RemoveUnclaimedWidget = [&]()
    {
        if (HUD->GetOptionMenuWidget() != Widget.Get())
        {
            Widget->OnMenuHidden();
            if (HUD->GetOptionMenuWidget() != Widget.Get()) { Widget->RemoveFromParent(); }
        }
    };
    if (!IsCurrentAttempt())
    {
        RemoveUnclaimedWidget();
        return;
    }
    if (!bReuseWidget) { Widget->SetVisibility(ESlateVisibility::Collapsed); }
    if (!IsCurrentAttempt())
    {
        RemoveUnclaimedWidget();
        return;
    }
    // Record cleanup ownership before provider and widget lifecycle callbacks.
    OptionsHUD = HUD.Get();
    OptionsWorld = World.Get();
    OptionsWidget = Widget.Get();
    const bool bAccepted = HUD->SetOptionMenuProvider(this, Widget.Get());
    if (!bAccepted || !IsCurrentAttempt() || !HasActiveOptionsSession())
    {
        if (Generation == OptionsGeneration) { ReleaseOptions(false); }
        RemoveUnclaimedWidget();
        return;
    }
    const bool bAdded = Widget->IsInViewport() || Widget->AddToPlayerScreen(10);
    if (!IsCurrentAttempt() || !HasActiveOptionsSession() || !bAdded)
    {
        if (Generation == OptionsGeneration) { ReleaseOptions(true); }
        RemoveUnclaimedWidget();
        return;
    }
    UpdateInputBindings();
    if (IsCurrentAttempt() && HasActiveOptionsSession())
    {
        UE_LOG(LogRPGOptionsUI, Display, TEXT("RPG_OPTIONS APPLIED World=%s HUD=%s Widget=%s Reused=%d"),
            *World->GetName(), *HUD->GetName(), *Widget->GetName(), bReuseWidget);
    }
}

void URPGOptionsMenuComponent::ReleaseOptions(bool bRestoreDefault)
{
    if (bReleasingOptions) { return; }
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(
        !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
    TGuardValue<bool> ReleasingGuard(bReleasingOptions, true);
    ++OptionsGeneration;
    CancelPendingRefresh();
    TStrongObjectPtr<ARPGHUD> HUD(OptionsHUD.Get());
    TStrongObjectPtr<URPGOptionMenu> Widget(OptionsWidget.Get());
    const TWeakObjectPtr<UWorld> World(OptionsWorld);
    TStrongObjectPtr<UEnhancedInputComponent> ReleasedInput(IsValid(ShortcutInput) ? ShortcutInput.Get() : nullptr);
    const TWeakObjectPtr<APlayerController> ReleasedController(ShortcutController);
    OptionsWidget = nullptr;
    OptionsHUD.Reset();
    OptionsWorld.Reset();
    ShortcutInput = nullptr;
    ShortcutController.Reset();
    ShortcutBindingHandles.Reset();
    if (HUD.IsValid() && Widget.IsValid() && HUD->IsOptionMenuProvider(this, Widget.Get()))
    {
        HUD->ClearOptionMenuProvider(this, bRestoreDefault && World.IsValid()
            && !World->bIsTearingDown && HUD->GetWorld() == World.Get() && GetWorld() == World.Get());
    }
    else if (Widget.IsValid() && (!HUD.IsValid() || HUD->GetOptionMenuWidget() != Widget.Get()))
    {
        Widget->OnMenuHidden();
        if (!HUD.IsValid() || HUD->GetOptionMenuWidget() != Widget.Get()) { Widget->RemoveFromParent(); }
    }
    // Release the captured old input after HUD ownership is detached, so its callbacks can claim a new provider.
    ReleaseOptionsInputComponent(ReleasedInput.Get(), ReleasedController.Get());
    if (Widget.IsValid())
    {
        UE_LOG(LogRPGOptionsUI, Display, TEXT("RPG_OPTIONS RELEASED HUD=%s"), *GetNameSafe(GetOwner()));
    }
}

void URPGOptionsMenuComponent::UpdateInputBindings()
{
    if (bUpdatingShortcuts)
    {
        QueueOptionsRefresh();
        return;
    }
    TStrongObjectPtr<URPGOptionsMenuComponent> KeepAlive(this);
    TGuardValue<bool> UpdatingGuard(bUpdatingShortcuts, true);
    if (!HasActiveOptionsSession() || ToggleActions.IsEmpty())
    {
        ReleaseInputBindings();
        return;
    }
    TStrongObjectPtr<ARPGHUD> HUD(OptionsHUD.Get());
    TStrongObjectPtr<APlayerController> Controller(HUD->GetOwningPlayerController());
    const uint64 Generation = OptionsGeneration;
    if (ShortcutInput && (ShortcutController.Get() != Controller.Get() || ShortcutInput->IsBeingDestroyed()))
    {
        ReleaseInputBindings();
    }
    if (Generation != OptionsGeneration || !HasActiveOptionsSession() || ToggleActions.IsEmpty()) { return; }
    if (!ShortcutInput)
    {
        ShortcutInput = NewObject<UEnhancedInputComponent>(HUD.Get());
        ShortcutInput->bBlockInput = false;
        ShortcutController = Controller.Get();
        TStrongObjectPtr<UEnhancedInputComponent> Input(ShortcutInput.Get());
        Input->RegisterComponent();
        if (Generation != OptionsGeneration || ShortcutInput != Input.Get() || !HasActiveOptionsSession()) { return; }
        if (!Input->IsRegistered() || Input->IsBeingDestroyed()) { ReleaseInputBindings(); return; }
        Controller->PushInputComponent(Input.Get());
        if (Generation != OptionsGeneration || ShortcutInput != Input.Get() || !HasActiveOptionsSession())
        {
            Controller->PopInputComponent(Input.Get());
            return;
        }
    }
    for (uint32 Handle : ShortcutBindingHandles) { ShortcutInput->RemoveBindingByHandle(Handle); }
    ShortcutBindingHandles.Reset();
    for (const auto& Pair : ToggleActions)
    {
        ShortcutBindingHandles.Add(ShortcutInput->BindAction(Pair.Key.Get(), ETriggerEvent::Started,
            this, &ThisClass::ToggleOptionsFromInput).GetHandle());
    }
}

void URPGOptionsMenuComponent::ReleaseInputBindings()
{
    TStrongObjectPtr<UEnhancedInputComponent> Input(IsValid(ShortcutInput) ? ShortcutInput.Get() : nullptr);
    const TWeakObjectPtr<APlayerController> Controller(ShortcutController);
    ShortcutInput = nullptr;
    ShortcutController.Reset();
    ShortcutBindingHandles.Reset();
    ReleaseOptionsInputComponent(Input.Get(), Controller.Get());
}

void URPGOptionsMenuComponent::ToggleOptionsFromInput()
{
    if (!bAcquiringOptions && !bUpdatingShortcuts && HasShortcutBindings())
    {
        TStrongObjectPtr<ARPGHUD> HUD(OptionsHUD.Get());
        HUD->ToggleOptionMenu();
    }
}
