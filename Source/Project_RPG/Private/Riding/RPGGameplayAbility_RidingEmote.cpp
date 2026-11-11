#include "Riding/RPGGameplayAbility_RidingEmote.h"
#include "Riding/RPGRidingComponent.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"

URPGGameplayAbility_RidingEmote::URPGGameplayAbility_RidingEmote()
{
    InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
    bServerRespectsRemoteAbilityCancellation=false;
}
bool URPGGameplayAbility_RidingEmote::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    const FGameplayTagContainer* Source, const FGameplayTagContainer* Target, FGameplayTagContainer* Failures) const
{
    auto* ASC=Info?Info->AbilitySystemComponent.Get():nullptr;
    const auto* Spec=ASC?ASC->FindAbilitySpecFromHandle(Handle):nullptr;
    const auto* C=Spec?Cast<URPGRidingComponent>(Spec->SourceObject.Get()):nullptr;
    if(!C || Info->AvatarActor.Get()!=C->GetOwner() || Spec->PendingRemove || Spec->RemoveAfterActivation)return false;
    const bool Allowed=C->GetOwner()->HasAuthority()
        ? C->OwnsEmoteAbility(ASC,Handle) && C->EvaluateEmoteAdmission()==ERPGRidingResultCode::Applied : C->CanRequestEmote();
    return Allowed && Super::CanActivateAbility(Handle,Info,Source,Target,Failures);
}
void URPGGameplayAbility_RidingEmote::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
    auto* ASC=Info?Info->AbilitySystemComponent.Get():nullptr; const auto* Spec=ASC?ASC->FindAbilitySpecFromHandle(Handle):nullptr;
    auto* C=Spec?Cast<URPGRidingComponent>(Spec->SourceObject.Get()):nullptr;
    if(!C || !C->OwnsEmoteAbility(ASC,Handle) || !CommitAbility(Handle,Info,Activation) || !IsActive() || !C->OwnsEmoteAbility(ASC,Handle))
    {EndAbility(Handle,Info,Activation,true,true);return;}
    Riding=C; ChangedHandle=C->OnChanged.AddUObject(this,&ThisClass::RidingChanged);
    GetWorld()->GetTimerManager().SetTimer(WatchTimer,this,&ThisClass::WatchExecution,.05f,true);
    bool Began;
    {TGuardValue<bool> Starting(bStarting,true);Began=C->BeginEmote(Ticket);}
    if(IsActive() && (!Began || !C->MatchesEmote(Ticket))) EndAbility(Handle,Info,Activation,true,true);
}
void URPGGameplayAbility_RidingEmote::RidingChanged(const FRPGRidingRepState&)
{
    if(bStarting || !IsActive())return;
    auto* C=Riding.Get();if(!C || !C->MatchesEmote(Ticket))EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}
void URPGGameplayAbility_RidingEmote::WatchExecution()
{
    if(bStarting || !IsActive())return;
    auto* C=Riding.Get();if(C)C->ValidateServerMovementContext();
    RidingChanged({});
}
void URPGGameplayAbility_RidingEmote::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled)
{
    if(!IsEndAbilityValid(Handle,Info))return;
    if(ScopeLockCount>0){WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this,&ThisClass::EndAbility,Handle,Info,Activation,Replicate,Cancelled));return;}
    auto* C=Riding.Get();const auto EndingTicket=Ticket;auto* ASC=Info?Info->AbilitySystemComponent.Get():nullptr;
    UWorld* EndingWorld=C?C->GetWorld():ASC?ASC->GetWorld():GetWorld();
    if(EndingWorld)EndingWorld->GetTimerManager().ClearTimer(WatchTimer);
    if(C)C->OnChanged.Remove(ChangedHandle);
    Riding.Reset();Ticket={};ChangedHandle.Reset();
    {TGuardValue<bool> Ending(bIsAbilityEnding,true);if(C)C->FinishEmote(EndingTicket,ERPGRidingEndReason::ExecutionRejected);}
    Super::EndAbility(Handle,Info,Activation,Replicate && Info && Info->OwnerActor.IsValid(),Cancelled);
}
