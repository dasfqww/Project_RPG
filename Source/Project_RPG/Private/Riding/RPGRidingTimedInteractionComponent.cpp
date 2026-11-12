#include "Riding/RPGRidingTimedInteractionComponent.h"
#include "Riding/RPGRidingTimedInteractionLease.h"
#include "Riding/RPGRidingInteractionScope.h"
#include "Riding/RPGRidingComponent.h"
#include "Interface/InteractionInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"

namespace
{
    bool ValidDuration(float Value) { return FMath::IsFinite(Value) && Value > .1f && Value <= 60.f; }
    bool LivingTimedAvatar(APawn* Pawn)
    {
        const auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true);
        if (!ASC || ASC->GetAvatarActor() != Pawn) return false;
        for (const TCHAR* Name : {TEXT("Shared.Status.Death"), TEXT("Shared.Status.Control.Stunned"),
            TEXT("Shared.Status.Control.Knockback"), TEXT("Shared.Status.Control.Airborne"), TEXT("Shared.Status.Control.Staggered")})
        {
            const auto Tag = FGameplayTag::RequestGameplayTag(FName(Name), false);
            if (Tag.IsValid() && ASC->HasMatchingGameplayTag(Tag)) return false;
        }
        for (const auto* Set : ASC->GetSpawnedAttributes())
        {
            const FGameplayAttribute Health(Set ? FindFProperty<FProperty>(Set->GetClass(), TEXT("CurrentHealth")) : nullptr);
            if (Health.IsValid()) { const float Value = ASC->GetNumericAttribute(Health); return FMath::IsFinite(Value) && Value > 0; }
        }
        return false;
    }
}
URPGRidingTimedInteractionComponent::URPGRidingTimedInteractionComponent()
{
    SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false; PrimaryComponentTick.TickInterval = .1f;
}
void URPGRidingTimedInteractionComponent::UpdateTick() { SetComponentTickEnabled(HasRequest() || Server.Sequence != 0); }
bool URPGRidingTimedInteractionComponent::Matches(const FContext& C, bool bServer, bool bExactState) const
{
    const auto* PC = Cast<APlayerController>(GetOwner()); auto* P = C.Pawn.Get(); const auto* T = C.Target.Get(); const auto* R = C.Riding.Get();
    const auto* Interaction = T ? Cast<IInteractionInterface>(T) : nullptr;
    if (bEnding || !IsRegistered() || !C.Sequence || !IsValid(PC) || PC->IsActorBeingDestroyed()
        || (bServer ? !PC->HasAuthority() : !PC->IsLocalController()) || !P || P->IsActorBeingDestroyed()
        || PC->GetPawn() != P || P->GetController() != PC || P->GetPlayerState() != C.PlayerState.Get()
        || !T || T->IsActorBeingDestroyed() || !R || !R->IsRegistered() || P->FindComponentByClass<URPGRidingComponent>() != R
        || !GetWorld() || GetWorld()->bIsTearingDown || P->GetWorld() != GetWorld() || T->GetWorld() != GetWorld()
        || !Interaction || !C.Identity.IsValid() || Interaction->GetRidingTimedInteractionIdentity() != C.Identity
        || !ValidDuration(C.Duration) || Interaction->InteractableData.InteractionDuration != C.Duration
        || !Interaction->CanApproveRidingTimedInteraction(const_cast<APlayerController*>(PC))
        || (bServer && (!P->HasAuthority() || !T->HasAuthority() || !LivingTimedAvatar(P)))) return false;
    const auto State = R->GetRidingState();
    if (!C.Key.RiderEpoch.IsValid() || State.Key.RiderEpoch != C.Key.RiderEpoch) return false;
    return !bExactState || (State.Mode == ERPGRidingMode::OnFoot && RPGRiding::Compare(State.Key, C.Key) == ERPGRidingStateOrder::Same);
}
bool URPGRidingTimedInteractionComponent::Prepare(AActor& InTarget, TFunction<void(float)> Ready,
    TFunction<void()> Complete, TFunction<void()> Interrupted)
{
    auto* PC = Cast<APlayerController>(GetOwner()); APawn* P = PC ? PC->GetPawn() : nullptr;
    auto* R = P ? P->FindComponentByClass<URPGRidingComponent>() : nullptr;
    const auto* Interaction = IsValid(&InTarget) ? Cast<IInteractionInterface>(&InTarget) : nullptr;
    if (bEnding || bPreparing || !IsRegistered() || HasRequest() || !PC || !PC->IsLocalController() || !P || !R || !Interaction
        || !Ready || !Complete || !Interrupted || LocalSequence == MAX_uint32 || R->GetRidingState().Key.StateRevision >= MAX_uint32 - 1) return false;
    TGuardValue<bool> Preparing(bPreparing, true);
    Local = {}; Local.Sequence = ++LocalSequence; Local.Pawn = P; Local.PlayerState = P->GetPlayerState();
    Local.Target = &InTarget; Local.Riding = R; Local.Key = Requested = R->GetRidingState().Key;
    Local.Identity = Interaction->GetRidingTimedInteractionIdentity(); Local.Duration = Interaction->InteractableData.InteractionDuration;
    if (!Matches(Local, false, false)) { ClearLocal(); return false; }
    bRequestedMounted = R->GetRidingState().Mode == ERPGRidingMode::Mounted;
    Phase = EPhase::Starting; LocalDeadline = FPlatformTime::Seconds() + 5;
    OnReady = MoveTemp(Ready); OnComplete = MoveTemp(Complete); OnInterrupted = MoveTemp(Interrupted); UpdateTick(); return true;
}
bool URPGRidingTimedInteractionComponent::Begin(AActor& InTarget, TFunction<void(float)> Ready,
    TFunction<void()> Complete, TFunction<void()> Interrupted)
{
    if (!Prepare(InTarget, MoveTemp(Ready), MoveTemp(Complete), MoveTemp(Interrupted))) return false;
    ServerBegin(Local.Sequence, Local.Pawn.Get(), Local.Target.Get(), Local.Identity, Requested); return true;
}
void URPGRidingTimedInteractionComponent::ServerBegin_Implementation(uint32 InSequence, APawn* InPawn, AActor* InTarget,
    FGuid InIdentity, FRPGRidingStateKey InExpected)
{
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!PC || !PC->HasAuthority() || !InSequence || InSequence <= ServerSequence) return;
    ServerSequence = InSequence;
    auto Reject = [&] { ClientStarted(InSequence, false, {}, 0); };
    const double Now = FPlatformTime::Seconds();
    if (bEnding || !IsRegistered() || Server.Sequence || Now < NextServerRequest) { Reject(); return; }
    NextServerRequest = Now + .2;
    auto* R = IsValid(InPawn) ? InPawn->FindComponentByClass<URPGRidingComponent>() : nullptr;
    const auto* Interaction = IsValid(InTarget) ? Cast<IInteractionInterface>(InTarget) : nullptr;
    if (PC->GetPawn() != InPawn || !R || !Interaction || InExpected.StateRevision >= MAX_uint32 - 1
        || R->GetRidingState().Key.RiderEpoch != InExpected.RiderEpoch
        || R->GetRidingState().Key.StateRevision != InExpected.StateRevision) { Reject(); return; }
    FContext Candidate; Candidate.Sequence = InSequence; Candidate.Pawn = InPawn; Candidate.PlayerState = InPawn->GetPlayerState();
    Candidate.Target = InTarget; Candidate.Riding = R; Candidate.Identity = InIdentity; Candidate.Key = InExpected;
    Candidate.Duration = Interaction->InteractableData.InteractionDuration;
    if (!FRPGRidingInteractionScope::TryExecute(*InPawn, *InTarget, [&] { return Matches(Candidate, true, false); }, [&]
        {
            auto Held = FRPGRidingTimedInteractionLease::TryAcquire(*InPawn); if (!Held) return false;
            Candidate.Key = R->GetRidingState().Key; Candidate.Started = FPlatformTime::Seconds();
            Candidate.Expires = Candidate.Started + Candidate.Duration + 10;
            Server = Candidate; Lease = MoveTemp(Held); UpdateTick();
            ClientStarted(InSequence, true, Server.Key, Server.Duration); return true;
        })) Reject();
}
void URPGRidingTimedInteractionComponent::ClientStarted_Implementation(uint32 InSequence, bool bAccepted, FRPGRidingStateKey InKey, float InDuration)
{
    if (!InSequence || Local.Sequence != InSequence || Phase != EPhase::Starting || bStartAck) return;
    const uint32 Revision = Requested.StateRevision + (bRequestedMounted ? 1 : 0);
    if (!bAccepted || !ValidDuration(InDuration) || InKey.RiderEpoch != Requested.RiderEpoch || InKey.StateRevision != Revision
        || InKey.ActionSequence < Requested.ActionSequence) { Cancel(); return; }
    bStartAck = true; Local.Key = InKey; Local.Duration = InDuration; PollLocal();
}
bool URPGRidingTimedInteractionComponent::Finish()
{
    if (Phase != EPhase::Active || !HasRequest()) return false;
    Phase = EPhase::Finishing; LocalDeadline = FPlatformTime::Seconds() + 5; ServerFinish(Local.Sequence); return true;
}
void URPGRidingTimedInteractionComponent::ServerFinish_Implementation(uint32 InSequence)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    if (!InSequence || Server.Sequence != InSequence) return;
    if (auto* R = Server.Riding.Get()) R->RefreshBinding();
    if (Server.Sequence != InSequence) return;
    const double Now = FPlatformTime::Seconds();
    if (!Matches(Server, true, true) || Now < Server.Started + Server.Duration || Now > Server.Expires)
    { StopServer(InSequence, true); return; }
    const auto Key = Server.Key; const auto Pin = MoveTemp(Lease); Server = {}; UpdateTick();
    ClientFinished(InSequence, true, Key); // Pin still blocks callback mounts until this notification returns.
}
void URPGRidingTimedInteractionComponent::ServerCancel_Implementation(uint32 InSequence)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    ServerSequence = FMath::Max(ServerSequence, InSequence); StopServer(InSequence, false);
}
void URPGRidingTimedInteractionComponent::StopServer(uint32 InSequence, bool bNotify)
{
    if (!InSequence || Server.Sequence != InSequence) return;
    const auto Pin = MoveTemp(Lease); Server = {}; UpdateTick();
    if (bNotify) ClientFinished(InSequence, false, {});
}
void URPGRidingTimedInteractionComponent::ClientFinished_Implementation(uint32 InSequence, bool bSuccess, FRPGRidingStateKey InKey)
{
    if (!InSequence || Local.Sequence != InSequence) return;
    if (!bSuccess) { EndLocal(false); return; }
    if (Phase != EPhase::Finishing || RPGRiding::Compare(InKey, Local.Key) != ERPGRidingStateOrder::Same) { Cancel(); return; }
    bFinishAck = true; PollLocal();
}
void URPGRidingTimedInteractionComponent::PollLocal()
{
    if (!HasRequest()) return;
    const bool Starting = Phase == EPhase::Starting;
    if (FPlatformTime::Seconds() >= LocalDeadline || !Matches(Local, false, !Starting)) { Cancel(); return; }
    if (Starting)
    {
        const auto State = Local.Riding->GetRidingState();
        if (State.Key.StateRevision < Requested.StateRevision || State.Key.StateRevision > Requested.StateRevision + 1
            || (State.Mode == ERPGRidingMode::Mounted && State.Key.StateRevision != Requested.StateRevision)) { Cancel(); return; }
        if (!bStartAck) return;
        const auto Order = RPGRiding::Compare(State.Key, Local.Key);
        if (Order == ERPGRidingStateOrder::Older) return;
        if (Order != ERPGRidingStateOrder::Same || State.Mode != ERPGRidingMode::OnFoot) { Cancel(); return; }
        Phase = EPhase::Active; LocalDeadline = FPlatformTime::Seconds() + Local.Duration + 10;
        auto Ready = MoveTemp(OnReady); if (Ready) Ready(Local.Duration);
        return;
    }
    if (Phase == EPhase::Finishing && bFinishAck) EndLocal(true);
}
void URPGRidingTimedInteractionComponent::PollServer()
{
    if (!Server.Sequence) return;
    if (auto* R = Server.Riding.Get()) R->RefreshBinding();
    if (Server.Sequence && (FPlatformTime::Seconds() > Server.Expires || !Matches(Server, true, true))) StopServer(Server.Sequence, true);
}
void URPGRidingTimedInteractionComponent::ClearLocal()
{
    Local = {}; Requested = {}; Phase = EPhase::None; LocalDeadline = 0;
    bStartAck = bFinishAck = bRequestedMounted = false; OnReady = nullptr; OnComplete = nullptr; OnInterrupted = nullptr; UpdateTick();
}
void URPGRidingTimedInteractionComponent::EndLocal(bool bSuccess)
{
    auto Action = bSuccess ? MoveTemp(OnComplete) : MoveTemp(OnInterrupted); ClearLocal(); if (Action) Action();
}
void URPGRidingTimedInteractionComponent::Cancel()
{
    const uint32 Sequence = Local.Sequence; auto Interrupted = MoveTemp(OnInterrupted); ClearLocal();
    if (Sequence) ServerCancel(Sequence);
    if (Interrupted) Interrupted();
}
void URPGRidingTimedInteractionComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{ Super::TickComponent(Delta, Tick, Function); PollServer(); PollLocal(); }
void URPGRidingTimedInteractionComponent::OnUnregister()
{ Cancel(); StopServer(Server.Sequence, false); Super::OnUnregister(); }
void URPGRidingTimedInteractionComponent::EndPlay(const EEndPlayReason::Type Reason)
{ bEnding = true; Cancel(); StopServer(Server.Sequence, false); Super::EndPlay(Reason); }
