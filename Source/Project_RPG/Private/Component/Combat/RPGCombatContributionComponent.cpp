#include "Component/Combat/RPGCombatContributionComponent.h"

#include "Combat/RPGApprovedHitContext.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

URPGCombatContributionComponent::URPGCombatContributionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPGCombatContributionComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetForAvatarReuse();
}

void URPGCombatContributionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, State);
}

bool URPGCombatContributionComponent::ConfigureForNewAttempt(const FRPGCombatContributionReceiverProfile& Profile)
{
	if (bEnding || bResetting || !GetOwner() || !GetOwner()->HasAuthority() || !Profile.IsValid() || bApplying) return false;
	ReceiverProfile = Profile;
	ResetForAvatarReuse();
	return true;
}

void URPGCombatContributionComponent::ResetForAvatarReuse()
{
	if (bEnding || bResetting || !GetOwner() || !GetOwner()->HasAuthority()) return;
	TGuardValue<bool> Resetting(bResetting, true);
	const uint32 NextGeneration = State.Generation + 1;
	const uint32 PreviousRevision = State.Revision;
	State = FRPGCombatContributionState();
	State.Generation = NextGeneration;
	State.Revision = PreviousRevision;
	State.bEnabled = ReceiverProfile.IsValid() && ReceiverProfile.bEnabled;
	if (State.bEnabled)
	{
		State.StaggerThreshold = ReceiverProfile.StaggerThreshold;
		State.PartBreakThreshold = ReceiverProfile.PartBreakThreshold;
	}
	SeenActivations.Reset();
	Publish();
}

void URPGCombatContributionComponent::AcceptApprovedSkillHit(URPGGameplayAbility_SkillContainer& Ability,
	uint64 Activation, uint32 Generation, AActor* Source, const FRPGApprovedHitContext& Lifetime, const FRPGCombatContributionAmounts& Amounts)
{
	if (bEnding || bApplying || bResetting || !State.bEnabled || !Amounts.IsValid() || Activation == 0 || !Ability.IsActive()
		|| State.Generation != Generation || Ability.GetSkillActivationSerial() != Activation || !Lifetime.IsCurrent(Source, GetOwner())) return;
	for (auto It = SeenActivations.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	const TWeakObjectPtr<UGameplayAbility> Key(&Ability);
	if (const uint64* Seen = SeenActivations.Find(Key); Seen && *Seen == Activation) return;
	// Four players with eight equipped skills fit comfortably; never evict a live
	// reservation and thereby let the same activation contribute twice.
	if (!SeenActivations.Contains(Key) && SeenActivations.Num() >= 128) return;
	TGuardValue<bool> Applying(bApplying, true);
	SeenActivations.Add(Key, Activation);
	const float Stagger = FMath::Min(State.StaggerThreshold, State.Stagger + Amounts.Stagger);
	const float Part = FMath::Min(State.PartBreakThreshold, State.PartBreak + Amounts.PartBreak);
	if (Stagger == State.Stagger && Part == State.PartBreak) return;
	State.Stagger = Stagger;
	State.PartBreak = Part;
	State.bStaggerComplete = State.StaggerThreshold > 0.f && Stagger >= State.StaggerThreshold;
	State.bPartBroken = State.PartBreakThreshold > 0.f && Part >= State.PartBreakThreshold;
	Publish(); // Reserve/update before callbacks; no state is written after them.
}

void URPGCombatContributionComponent::Publish()
{
	++State.Revision;
	GetOwner()->ForceNetUpdate();
	OnContributionChanged.Broadcast(State);
}

void URPGCombatContributionComponent::OnRep_State() { OnContributionChanged.Broadcast(State); }

#if WITH_EDITOR
EDataValidationResult URPGCombatContributionComponent::IsDataValid(FDataValidationContext& Context) const
{
	const auto SuperResult = Super::IsDataValid(Context);
	if (!ReceiverProfile.IsValid())
	{
		Context.AddError(NSLOCTEXT("RPGCombatContribution", "InvalidReceiverProfile", "Enabled contribution receivers require finite nonnegative thresholds and at least one enabled mechanic."));
		return EDataValidationResult::Invalid;
	}
	return SuperResult == EDataValidationResult::Invalid ? SuperResult : EDataValidationResult::Valid;
}
#endif

void URPGCombatContributionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	SeenActivations.Reset();
	Super::EndPlay(Reason);
}
