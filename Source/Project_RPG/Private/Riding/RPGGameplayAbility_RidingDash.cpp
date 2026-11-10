#include "Riding/RPGGameplayAbility_RidingDash.h"
#include "Riding/RPGRidingComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

URPGGameplayAbility_RidingDash::URPGGameplayAbility_RidingDash()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    // ClientOrServer security permits GAS's owning-client activation request, never client execution.
    bRetriggerInstancedAbility = false;
}

bool URPGGameplayAbility_RidingDash::CanActivateAbility(FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* Info, const FGameplayTagContainer* Source,
    const FGameplayTagContainer* Target, FGameplayTagContainer* Failures) const
{
    auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
    const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
    auto* C = Spec ? Cast<URPGRidingComponent>(Spec->SourceObject.Get()) : nullptr;
    auto* Pawn = C ? Cast<APawn>(C->GetOwner()) : nullptr;
    if (!C || !C->IsRegistered() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()
        || Info->AvatarActor.Get() != Pawn || ASC->GetAvatarActor() != Pawn || ASC->GetWorld() != Pawn->GetWorld()
        || Spec->PendingRemove || Spec->RemoveAfterActivation) return false;
    if (Pawn->HasAuthority())
    {
        if (!C->OwnsDashAbility(ASC, Handle)
            || C->EvaluateDashAdmission(C->State.Key) != ERPGRidingResultCode::Applied) return false;
    }
    else if (!C->CanRequestDash()) return false;
    FRPGRidingDashGrant Grant;
    if (!C->TryGetDashGrant(Grant) || Grant.AbilityHandle != Handle) return false;
    return Super::CanActivateAbility(Handle, Info, Source, Target, Failures);
}

void URPGGameplayAbility_RidingDash::ActivateAbility(FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
    auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
    const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
    auto* C = Spec ? Cast<URPGRidingComponent>(Spec->SourceObject.Get()) : nullptr;
    auto* Character = C ? Cast<ACharacter>(C->GetOwner()) : nullptr;
    auto* Move = Character ? Character->GetCharacterMovement() : nullptr;
    FRPGRidingDashGrant Grant;
    if (!Character || !Character->HasAuthority() || !Move || !C->OwnsDashAbility(ASC, Handle)
        || Info->AvatarActor.Get() != Character || !C->TryGetDashGrant(Grant))
    { EndAbility(Handle, Info, Activation, true, true); return; }
    const FRPGRidingDashActivation Context{Grant, Activation.GetActivationPredictionKey()};
    if (!Context.IsValid() || !CommitAbility(Handle, Info, Activation) || !IsActive() || !C->IsDashActivationCurrent(Context))
    { EndAbility(Handle, Info, Activation, true, true); return; }

    // Use the server's current accepted movement input, falling back to its current facing.
    FVector Direction = Move->GetCurrentAcceleration(); Direction.Z = 0;
    if (!Direction.Normalize()) { Direction = Character->GetActorForwardVector(); Direction.Z = 0; Direction.Normalize(); }
    Riding = C;
    const FRPGRidingStateKey Expected = C->State.Key;
    StartingKey = Expected;
    ChangedHandle = C->OnChanged.AddUObject(this, &ThisClass::RidingChanged);
    ERPGRidingResultCode Result;
    {
        TGuardValue<bool> Starting(bStarting, true);
        Result = C->StartServerDash(Expected, Direction, Context);
    }
    // Admission callbacks may have ended this ability, revoked its spec or replaced the Pawn.
    if (!IsActive() || Riding.Get() != C) return;
    const auto State = C->State;
    if (State.Key.RiderEpoch == Expected.RiderEpoch && State.Key.StateRevision == Expected.StateRevision
        && State.Key.ActionSequence == Expected.ActionSequence + 1 && State.Action == ERPGRidingAction::Dash)
        Ticket = C->ActiveDash;
    if (Result != ERPGRidingResultCode::Applied || !C->IsDashActivationCurrent(Context) || !C->OwnsDashAbility(ASC, Handle)
        || State.Key.RiderEpoch != Expected.RiderEpoch || State.Key.StateRevision != Expected.StateRevision
        || State.Key.ActionSequence != Expected.ActionSequence + 1 || !C->IsServerDashActive())
    { EndAbility(Handle, Info, Activation, true, true); return; }
}

void URPGGameplayAbility_RidingDash::RidingChanged(const FRPGRidingRepState&)
{
    if (bStarting || !IsActive()) return;
    auto* C = Riding.Get();
    if (!C || !C->MatchesDash(Ticket))
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void URPGGameplayAbility_RidingDash::EndAbility(FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled)
{
    if (!IsEndAbilityValid(Handle, Info)) return;
    if (ScopeLockCount > 0)
    {
        WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &ThisClass::EndAbility, Handle, Info, Activation, Replicate, Cancelled));
        return;
    }
    auto EndingTicket = Ticket;
    auto* C = Riding.Get();
    // An earlier publication subscriber can cancel before our own initial state listener runs.
    if (!EndingTicket.ExecutionId.IsValid() && bStarting && C
        && C->State.Key.RiderEpoch == StartingKey.RiderEpoch && C->State.Key.StateRevision == StartingKey.StateRevision
        && C->State.Key.ActionSequence == StartingKey.ActionSequence + 1) EndingTicket = C->ActiveDash;
    if (C) C->OnChanged.Remove(ChangedHandle);
    ChangedHandle.Reset(); Riding.Reset(); Ticket = {}; StartingKey = {};
    {
        // Finishing motion publishes state and may reenter GAS. Never finish a newer execution.
        TGuardValue<bool> Ending(bIsAbilityEnding, true);
        if (C && C->MatchesDash(EndingTicket)) C->FinishDash(EndingTicket, ERPGRidingEndReason::ExecutionRejected);
    }
    Super::EndAbility(Handle, Info, Activation, Replicate, Cancelled);
}
