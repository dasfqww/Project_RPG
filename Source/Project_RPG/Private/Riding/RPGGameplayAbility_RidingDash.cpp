#include "Riding/RPGGameplayAbility_RidingDash.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingDashPrediction.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

URPGGameplayAbility_RidingDash::URPGGameplayAbility_RidingDash()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    // Owning-client prediction carries input only; all movement authority remains on the server.
    bServerRespectsRemoteAbilityCancellation = false;
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
    if (!Character || !Move || Info->AvatarActor.Get() != Character || !C->TryGetDashGrant(Grant))
    { EndAbility(Handle, Info, Activation, true, true); return; }
    Context = {Grant, Activation.GetActivationPredictionKey()};
    if (!Context.IsValid() || !C->IsDashActivationCurrent(Context))
    { EndAbility(Handle, Info, Activation, true, true); return; }
    Riding = C; StartingKey = C->State.Key;
    ChangedHandle = C->OnChanged.AddUObject(this, &ThisClass::RidingChanged);
    GetWorld()->GetTimerManager().SetTimer(WatchTimer, this, &ThisClass::WatchExecution, .05f, true);

    if (Character->HasAuthority() && !Info->IsLocallyControlled() && Context.PredictionKey.IsLocalClientKey())
    {
        // Reliable GAS activation precedes its target data; no window/cooldown is spent while waiting.
        bWaitingForIntent = true; IntentDeadline = GetWorld()->GetTimeSeconds() + 2.;
        DataHandle = ASC->AbilityTargetDataSetDelegate(Handle, Context.PredictionKey).AddUObject(this, &ThisClass::ReceiveIntent);
        CancelHandle = ASC->AbilityTargetDataCancelledDelegate(Handle, Context.PredictionKey).AddUObject(this, &ThisClass::CancelIntent);
        ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, Context.PredictionKey);
        return;
    }
    FVector Direction = Move->GetCurrentAcceleration(); Direction.Z = 0;
    if (!Direction.Normalize()) { Direction = Character->GetActorForwardVector(); Direction.Z = 0; Direction.Normalize(); }
    FRPGRidingDashIntent Intent; Intent.GrantId = Grant.GrantId; Intent.Expected = StartingKey; Intent.Direction = Direction;
    if (Character->HasAuthority()) { BeginServer(Intent); return; }
    if (!CommitAbility(Handle, Info, Activation) || !IsActive() || !C->DashPrediction->Start(Context, Intent))
    {
        if (Context.PredictionKey.IsLocalClientKey()) ASC->ServerSetReplicatedTargetDataCancelled(Handle, Context.PredictionKey, ASC->ScopedPredictionKey);
        EndAbility(Handle, Info, Activation, false, true); return;
    }
    FGameplayAbilityTargetDataHandle Data; Data.Add(new FRPGRidingDashIntent(Intent));
    // Both values come from this GAS activation scope, never from a UI- or RPC-selected execution ID.
    ASC->CallServerSetReplicatedTargetData(Handle, Context.PredictionKey, Data, {}, ASC->ScopedPredictionKey);
}

void URPGGameplayAbility_RidingDash::ReceiveIntent(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag)
{
    if (!IsActive() || !bWaitingForIntent) return;
    const auto* Value = FRPGRidingDashIntent::Read(Data);
    const auto Frozen = Value ? *Value : FRPGRidingDashIntent{};
    auto* ASC = GetAbilitySystemComponentFromActorInfo();
    if (ASC) ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, Context.PredictionKey);
    bWaitingForIntent = false;
    if (Tag.IsValid() || !Frozen.IsValid()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
    BeginServer(Frozen);
}
void URPGGameplayAbility_RidingDash::CancelIntent()
{
    if (IsActive() && bWaitingForIntent) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
void URPGGameplayAbility_RidingDash::BeginServer(const FRPGRidingDashIntent& Intent)
{
    auto* C = Riding.Get(); auto* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!IsActive() || !C || !C->GetOwner()->HasAuthority() || !Intent.IsValid() || Intent.GrantId != Context.Grant.GrantId
        || RPGRiding::Compare(Intent.Expected, StartingKey) != ERPGRidingStateOrder::Same || !C->IsDashActivationCurrent(Context)
        || !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) || !IsActive() || !C->IsDashActivationCurrent(Context))
    { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
    ERPGRidingResultCode Result;
    { TGuardValue<bool> Starting(bStarting, true); Result = C->StartServerDash(Intent.Expected, Intent.Direction, Context); }
    if (!IsActive() || Riding.Get() != C) return;
    const auto State = C->State;
    if (State.ExecutionId == Context.ExecutionFor(StartingKey) && State.Action == ERPGRidingAction::Dash) Ticket = C->ActiveDash;
    if (Result != ERPGRidingResultCode::Applied || !C->IsDashActivationCurrent(Context) || !C->OwnsDashAbility(ASC, CurrentSpecHandle)
        || State.ExecutionId != Context.ExecutionFor(StartingKey) || !C->IsServerDashActive())
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
void URPGGameplayAbility_RidingDash::WatchExecution()
{
    if (!IsActive() || bStarting) return;
    auto* C = Riding.Get();
    if (!C || !C->IsDashActivationCurrent(Context)) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
    if (C->GetOwner()->HasAuthority())
    {
        if (bWaitingForIntent && GetWorld()->GetTimeSeconds() >= IntentDeadline) CancelIntent();
        return;
    }
    C->DashPrediction->Update();
    if (!C->DashPrediction->IsPending()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}
void URPGGameplayAbility_RidingDash::RidingChanged(const FRPGRidingRepState&)
{
    if (bStarting || !IsActive()) return;
    auto* C = Riding.Get();
    if (C && !C->GetOwner()->HasAuthority()) { WatchExecution(); return; }
    if (!C || (bWaitingForIntent ? RPGRiding::Compare(C->State.Key, StartingKey) != ERPGRidingStateOrder::Same : !C->MatchesDash(Ticket)))
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
void URPGGameplayAbility_RidingDash::EndAbility(FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled)
{
    if (!IsEndAbilityValid(Handle, Info)) return;
    if (ScopeLockCount > 0)
    {
        WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &ThisClass::EndAbility, Handle, Info, Activation, Replicate, Cancelled)); return;
    }
    auto EndingTicket = Ticket; auto* C = Riding.Get(); auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
    if (!EndingTicket.ExecutionId.IsValid() && bStarting && C && C->State.ExecutionId == Context.ExecutionFor(StartingKey)) EndingTicket = C->ActiveDash;
    if (C) C->OnChanged.Remove(ChangedHandle);
    UWorld* EndingWorld = C ? C->GetWorld() : (ASC ? ASC->GetWorld() : GetWorld());
    if (EndingWorld) EndingWorld->GetTimerManager().ClearTimer(WatchTimer);
    if (ASC)
    {
        if (DataHandle.IsValid()) ASC->AbilityTargetDataSetDelegate(Handle, Activation.GetActivationPredictionKey()).Remove(DataHandle);
        if (CancelHandle.IsValid()) ASC->AbilityTargetDataCancelledDelegate(Handle, Activation.GetActivationPredictionKey()).Remove(CancelHandle);
        ASC->ConsumeAllReplicatedData(Handle, Activation.GetActivationPredictionKey());
    }
    ChangedHandle.Reset(); DataHandle.Reset(); CancelHandle.Reset(); Riding.Reset(); Ticket = {}; StartingKey = {}; Context = {}; bWaitingForIntent = false;
    {
        TGuardValue<bool> Ending(bIsAbilityEnding, true);
        if (C)
        {
            if (C->GetOwner()->HasAuthority()) { if (C->MatchesDash(EndingTicket)) C->FinishDash(EndingTicket, ERPGRidingEndReason::ExecutionRejected); }
            else C->DashPrediction->Stop();
        }
    }
    // ClearActorInfo can withdraw the grant before this callback. There is no owning RPC context then.
    Super::EndAbility(Handle, Info, Activation, Replicate && Info && Info->OwnerActor.IsValid(), Cancelled);
}
