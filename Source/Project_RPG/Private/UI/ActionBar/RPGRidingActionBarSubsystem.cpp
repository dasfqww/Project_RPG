#include "UI/ActionBar/RPGRidingActionBarSubsystem.h"
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/RPGActionBarIdentityWidget.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Controller/RPGPlayerController.h"
#include "RPGInputTags.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGMountDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "RPGRidingActionBar"

class FRPGRidingActionBarProvider final : public IRPGActionBarProvider
{
public:
    FRPGRidingActionBarProvider(URPGRidingComponent* InRiding, APlayerController* InController)
        : Riding(InRiding), Controller(InController), Epoch(InRiding->GetRidingState().Key.RiderEpoch),
          Profile(NewObject<URPGActionBarProfile>())
    {
        Profile->ProfileId = TEXT("Riding");
        Profile->NativeSlotCount = 4;
        Profile->CenterWidgetClass = URPGActionBarIdentityWidget::StaticClass();
        // PlayerIdentity is an authored fullscreen module; place this compact replacement explicitly.
        Profile->bOverrideCenterLayout = true;
        Profile->CenterLayout.Anchors = FAnchors(.5f, 1.f);
        Profile->CenterLayout.Alignment = FVector2D(.5f, 1.f);
        Profile->CenterLayout.Offsets = FMargin(0.f, -88.f, 240.f, 88.f);
        FRPGActionBarEntry Dash;
        Dash.ActionId = TEXT("Dash"); Dash.Label = LOCTEXT("Dash", "질주"); Dash.DisplaySlot = 0;
        // The existing dodge action owns the physical key, including player remaps.
        Dash.InputTag = RPGGameplayTags::InputTag_Roll;
        Profile->Actions.Add(Dash);
        FRPGActionBarEntry Entry;
        Entry.ActionId = TEXT("Dismount");
        Entry.Label = LOCTEXT("Dismount", "내리기");
        // Reuse the current fourth skill binding; never overwrite the player's physical key mapping.
        Entry.SkillSlot = 3;
        Entry.DisplaySlot = 3;
        Profile->Actions.Add(Entry);
        RefreshIdentity();
    }
    void RefreshIdentity()
    {
        const auto* C = Riding.Get();
        const auto* Definition = C ? C->GetPresentationDefinition() : nullptr;
        DisplayName = Definition && !Definition->DisplayName.IsEmpty()
            ? Definition->DisplayName : LOCTEXT("Mount", "탈것");
    }
    FGuid GetEpoch() const { return Epoch; }
    virtual APawn* GetActionBarPawn() const override { return Riding.IsValid() ? Cast<APawn>(Riding->GetOwner()) : nullptr; }
    virtual const URPGActionBarProfile* GetActionBarProfile() const override { return Profile.Get(); }
    virtual FRPGActionBarActionView ReadAction(FName Id) const override
    {
        if (Id == TEXT("Dash"))
        {
            auto View = ReadAction(TEXT("Dismount"));
            if (!View.bAvailable) return View;
            if (const auto* PC = Cast<ARPGPlayerController>(Controller.Get()))
            {
                const FKey Key = PC->GetCurrentKeyForTag(RPGGameplayTags::InputTag_Roll);
                if (Key.IsValid()) View.KeyLabel = Key.GetDisplayName(false);
            }
            auto* C = Riding.Get();
            View.bAvailable = C && C->CanRequestDash();
            View.UnavailableReason = LOCTEXT("DashUnavailable", "현재 질주를 사용할 수 없습니다.");
            const auto* PS = GetActionBarPawn()->GetPlayerState();
            const auto* S = PS ? PS->FindComponentByClass<URPGRidingSessionComponent>() : nullptr;
            const auto* Clock = C->GetWorld()->GetGameState();
            if (S && Clock)
            {
                const double Remaining = S->GetOwnerState().DashEndServerSeconds - Clock->GetServerWorldTimeSeconds();
                if (FMath::IsFinite(Remaining)) View.CooldownRemaining = static_cast<float>(FMath::Clamp(Remaining, 0., 3600.));
            }
            return View;
        }
        FRPGActionBarActionView View;
        auto* P = GetActionBarPawn();
        auto* PC = Controller.Get();
        const auto* C = Riding.Get();
        if (Id == TEXT("Dismount") && IsValid(P) && !P->IsActorBeingDestroyed() && C && C->IsRegistered()
            && IsValid(PC) && !PC->IsActorBeingDestroyed() && PC->IsLocalController()
            && PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetPlayerController(PC->GetWorld()) == PC
            && PC->GetPawn() == P && P->GetController() == PC && P->GetWorld() == PC->GetWorld())
        {
            const auto State = C->GetRidingState();
            View.bAvailable = RPGRiding::IsCanonical(State) && State.Mode == ERPGRidingMode::Mounted
                && Epoch.IsValid() && State.Key.RiderEpoch == Epoch;
        }
        if (!View.bAvailable) View.UnavailableReason = LOCTEXT("StateChanged", "탑승 상태를 다시 확인 중입니다.");
        return View;
    }
    virtual FRPGActionBarIdentityView ReadIdentity() const override
    {
        FRPGActionBarIdentityView View;
        if (!ReadAction(TEXT("Dismount")).bAvailable) return View;
        const auto State = Riding->GetRidingState();
        View.bVisible = true;
        View.Title = DisplayName;
        View.Status = LOCTEXT("Mounted", "탑승 중");
        View.Detail = FText::Format(LOCTEXT("BaseSpeed", "기본 이동 속도 {0}"), FText::AsNumber(State.Movement.Speed));
        return View;
    }
    virtual void PressAction(FName Id) override
    {
        if (ReadAction(Id).bAvailable)
            if (auto* C = Riding.Get())
            {
                if (Id == TEXT("Dash")) C->RequestDash();
                else if (Id == TEXT("Dismount")) C->RequestSetMounted(false, FRPGRidingSelector{});
            }
    }
    virtual void ReleaseAction(FName) override {}
private:
    TWeakObjectPtr<URPGRidingComponent> Riding;
    TWeakObjectPtr<APlayerController> Controller;
    FGuid Epoch;
    TStrongObjectPtr<URPGActionBarProfile> Profile;
    FText DisplayName;
};

void URPGRidingActionBarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bEnding = false;
    PlayerControllerChanged(GetLocalPlayer()->GetPlayerController(GetWorld()));
}

bool URPGRidingActionBarSubsystem::IsControllerCurrent() const
{
    auto* PC = Controller.Get();
    auto* World = GetWorld();
    return !bEnding && World && !World->bIsTearingDown && IsValid(PC) && !PC->IsActorBeingDestroyed()
        && PC->IsLocalController() && PC->GetLocalPlayer() == GetLocalPlayer() && PC->GetWorld() == World
        && GetLocalPlayer()->GetPlayerController(World) == PC;
}

void URPGRidingActionBarSubsystem::PlayerControllerChanged(APlayerController* NewController)
{
    if (bEnding) return;
    const uint64 Revision = ++BindingRevision;
    if (auto* Previous = Controller.Get()) Previous->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePawnChanged);
    FTSTicker::GetCoreTicker().RemoveTicker(WatchHandle);
    WatchHandle.Reset();
    DropPawn();
    if (bEnding || Revision != BindingRevision) return;
    Controller = NewController;
    if (!IsControllerCurrent()) { Controller.Reset(); return; }
    NewController->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::HandlePawnChanged);
    // Observe late registration/unregistration as well as possession and replication events.
    WatchHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::WatchBinding), 0.1f);
    RefreshPawn();
}

bool URPGRidingActionBarSubsystem::WatchBinding(float)
{
    if (!IsControllerCurrent())
    {
        PlayerControllerChanged(nullptr);
        return false;
    }
    RefreshPawn();
    return true;
}

void URPGRidingActionBarSubsystem::HandlePawnChanged(APawn*, APawn*) { RefreshPawn(); }
void URPGRidingActionBarSubsystem::HandleRidingChanged(const FRPGRidingRepState&) { RefreshPawn(); }

void URPGRidingActionBarSubsystem::DropPawn()
{
    auto* OldRiding = Riding.Get();
    auto* OldContext = Context.Get();
    const auto OldProvider = Provider;
    if (OldRiding) OldRiding->OnChanged.Remove(RidingHandle);
    if (OldContext) OldContext->OnChanged.Remove(ContextHandle);
    RidingHandle.Reset(); ContextHandle.Reset();
    Pawn.Reset(); Riding.Reset(); Context.Reset(); Provider.Reset();
    if (OldContext && OldProvider) OldContext->Deactivate(OldProvider.ToSharedRef());
}

void URPGRidingActionBarSubsystem::RefreshPawn()
{
    APawn* NextPawn = IsControllerCurrent() ? Controller->GetPawn() : nullptr;
    if (NextPawn && (NextPawn->IsActorBeingDestroyed() || NextPawn->GetWorld() != GetWorld())) NextPawn = nullptr;
    auto* NextRiding = NextPawn ? NextPawn->FindComponentByClass<URPGRidingComponent>() : nullptr;
    auto* Slots = NextPawn ? NextPawn->FindComponentByClass<UQuickSlotComponent>() : nullptr;
    if (NextRiding && !NextRiding->IsRegistered()) NextRiding = nullptr;
    auto* NextContext = Slots && Slots->IsRegistered() ? Slots->GetActionBarContext() : nullptr;
    if (Pawn != NextPawn || Riding != NextRiding || Context != NextContext)
    {
        const uint64 Revision = ++BindingRevision;
        DropPawn();
        if (Revision != BindingRevision || !IsControllerCurrent() || Controller->GetPawn() != NextPawn) return;
        Pawn = NextPawn; Riding = NextRiding; Context = NextContext;
        if (NextRiding) RidingHandle = NextRiding->OnChanged.AddUObject(this, &ThisClass::HandleRidingChanged);
        if (NextContext) ContextHandle = NextContext->OnChanged.AddUObject(this, &ThisClass::Reconcile);
    }
    Reconcile();
}

void URPGRidingActionBarSubsystem::Reconcile()
{
    if (bReconciling || bEnding) return;
    TGuardValue<bool> Guard(bReconciling, true);
    auto* C = Context.Get();
    auto* R = Riding.Get();
    const auto State = R ? R->GetRidingState() : FRPGRidingRepState{};
    const bool bMounted = IsControllerCurrent() && Pawn.IsValid() && Controller->GetPawn() == Pawn.Get()
        && R && R->IsRegistered() && C && RPGRiding::IsCanonical(State) && State.Mode == ERPGRidingMode::Mounted;
    if (!bMounted)
    {
        if (C && Provider) C->Deactivate(Provider.ToSharedRef());
        Provider.Reset();
        return;
    }
    // A different feature owns the visible profile. Do not steal it on every Riding update.
    if (C->HasOverride() && (!Provider || C->GetProfile() != Provider->GetActionBarProfile())) return;
    if (!C->HasOverride() && C->ConsumesCombatInput()) return;
    if (!Provider || Provider->GetEpoch() != State.Key.RiderEpoch)
        Provider = MakeShared<FRPGRidingActionBarProvider>(R, Controller.Get());
    Provider->RefreshIdentity();
    C->Activate(Provider.ToSharedRef());
}

void URPGRidingActionBarSubsystem::Deinitialize()
{
    if (bEnding) return;
    bEnding = true;
    ++BindingRevision;
    if (auto* PC = Controller.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePawnChanged);
    FTSTicker::GetCoreTicker().RemoveTicker(WatchHandle);
    WatchHandle.Reset();
    Controller.Reset();
    DropPawn();
    Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE
