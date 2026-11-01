#include "Ability/RPGBossTelegraphedStrikeAbility.h"

#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Security/RPGSecurityTypes.h"

namespace
{
	bool IsStrikeTargetInRange(const APawn* Avatar, const APawn* Target, float MaximumDistance)
	{
		if (!IsValid(Avatar) || !IsValid(Target) || !FMath::IsFinite(MaximumDistance) || MaximumDistance <= 0.f) { return false; }
		// Reuse the damage validator's bounds allowance; do not tighten reach for large targets.
		FRPGCombatHitSecuritySample Sample;
		Sample.SourceLocation = Avatar->GetActorLocation();
		Target->GetActorBounds(true, Sample.TargetBoundsOrigin, Sample.TargetBoundsExtent, false);
		Sample.ImpactPoint = Sample.TargetBoundsOrigin;
		Sample.MaximumDistance = MaximumDistance;
		Sample.HitLocationTolerance = 0.f;
		return FRPGSecurityValidationMath::ValidateCombatHit(Sample).bValid;
	}
}

bool URPGBossTelegraphedStrikeAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	FString ProfileError;
	const APawn* Avatar = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	const URPGBossAIComponent* BossStateComponent = Avatar ? Avatar->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const URPGBossThreatComponent* TargetSelection = Avatar ? Avatar->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	const UGameplayEffect* Effect = DamageEffectClass ? DamageEffectClass->GetDefaultObject<UGameplayEffect>() : nullptr;
	const float Damage = BaseDamage.GetValueAtLevel(GetAbilityLevel());
	return BossStateComponent && !BossStateComponent->GetTelegraphState().bActive &&
		ActionTag.IsValid() && FMath::IsFinite(WindupSeconds) && WindupSeconds >= .01f &&
		WindupSeconds <= 120.f && FMath::IsFinite(Radius) && Radius > 0.f && Radius <= 2500.f &&
		FMath::IsFinite(Damage) && Damage > 0.f && SecurityProfile.bRequireAuthorityHitQuery &&
		FMath::IsFinite(RecoverySeconds) && RecoverySeconds >= 0.f && RecoverySeconds <= 5.f &&
		(!PresentationMontage || (!PresentationMontage->HasRootMotion() &&
			FMath::IsFinite(PresentationPlayRate) && PresentationPlayRate >= .01f && PresentationPlayRate <= 10.f)) &&
		SecurityProfile.IsValid(&ProfileError) && Damage <= SecurityProfile.MaximumDamagePerHit &&
		Effect && Effect->DurationPolicy == EGameplayEffectDurationType::Instant && Effect->Period.GetValueAtLevel(1.f) == 0.f &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) &&
		IsValid(TargetSelection) && IsStrikeTargetInRange(Avatar, TargetSelection->GetCurrentTarget(), SecurityProfile.MaximumServerHitDistance);
}

void URPGBossTelegraphedStrikeAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive() || !IsBossExecutionValid()) { return; }
	APawn* Target = GetBossExecutionTarget();
	UWorld* World = GetWorld();
	const uint64 StartedSerial = GetBossExecutionSerial();
	const APawn* Avatar = Cast<APawn>(GetAvatarActorFromActorInfo());
	// Threat acquisition is not attack reach. Reject before committing costs/cooldown or publishing a warning.
	const bool bCommitted = World && IsStrikeTargetInRange(Avatar, Target, SecurityProfile.MaximumServerHitDistance) &&
		CommitAbility(Handle, ActorInfo, ActivationInfo);
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	if (!bCommitted || !IsBossExecutionValid() || !IsStrikeTargetInRange(Avatar, Target, SecurityProfile.MaximumServerHitDistance))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	LockedAimLocation = Target->GetActorLocation();
	LockedEncounterId = GetBossExecutionEncounterId();
	const int32 StartedSequence = StartBossTelegraph(ActionTag, LockedAimLocation, WindupSeconds);
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	LockedTelegraphSequence = StartedSequence;
	if (LockedTelegraphSequence == 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	World->GetTimerManager().SetTimer(StrikeTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, StartedSerial]() { ResolveStrike(StartedSerial); }),
		WindupSeconds, false);
	// No montage notify may authorize damage or movement. ASC carries montage replication.
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ExecutionMontage = PresentationMontage;
	if (ASC && PresentationMontage)
	{
		ASC->PlayMontage(this, ActivationInfo, PresentationMontage, PresentationPlayRate);
	}
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial || !IsBossExecutionValid()) { return; }
	if (ASC && WindupCueTag.IsValid())
	{
		FGameplayCueParameters Parameters;
		Parameters.Instigator = GetAvatarActorFromActorInfo();
		Parameters.Location = LockedAimLocation;
		ASC->ExecuteGameplayCue(WindupCueTag, Parameters);
	}
}

void URPGBossTelegraphedStrikeAbility::ResolveStrike(uint64 StartedSerial)
{
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	APawn* Avatar = Cast<APawn>(GetAvatarActorFromActorInfo());
	const URPGBossAIComponent* BossStateComponent = Avatar ? Avatar->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const FRPGBossTelegraphState Telegraph = BossStateComponent ? BossStateComponent->GetTelegraphState() : FRPGBossTelegraphState();
	if (!IsBossExecutionValid() || !Telegraph.bActive || Telegraph.EncounterId != LockedEncounterId ||
		Telegraph.Sequence != LockedTelegraphSequence || !FinishBossTelegraph())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	// End listeners may cancel or restart this per-actor ability. Never hit on behalf of the old execution.
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial || !IsBossExecutionValid()) { return; }
	FRPGHitQueryProfile HitProfile;
	HitProfile.Shape.Type = ERPGHitQueryShape::Sphere;
	HitProfile.Shape.Radius = Radius;
	HitProfile.Filter.TeamRule = ERPGHitQueryTeamRule::HostileOnly;
	// The shared query clamps this to SecurityProfile.MaximumTargetsPerQuery before finding the captured target.
	HitProfile.Filter.MaxResults = 0;
	if (bHitAllThreatParticipants)
	{
		TArray<FRPGHitQueryResult> Hits;
		ExecuteAreaServerDamage(FTransform(FQuat::Identity, LockedAimLocation), HitProfile,
			DamageEffectClass, BaseDamage, SecurityProfile, Hits);
	}
	else
	{
		FRPGHitQueryResult Hit;
		ExecuteTargetedServerDamage(FTransform(FQuat::Identity, LockedAimLocation), HitProfile,
			DamageEffectClass, BaseDamage, SecurityProfile, Hit);
	}
	if (IsActive() && GetBossExecutionSerial() == StartedSerial)
	{
		if (RecoverySeconds > 0.f && IsBossExecutionValid())
		{
			GetWorld()->GetTimerManager().SetTimer(StrikeTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, StartedSerial]()
				{
					if (IsActive() && GetBossExecutionSerial() == StartedSerial)
						EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !IsBossExecutionValid());
				}), RecoverySeconds, false);
			return;
		}
		// A completed swing may miss; BT completion is not a hit confirmation.
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void URPGBossTelegraphedStrikeAbility::EndAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) { return; }
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(StrikeTimer); }
	LockedTelegraphSequence = 0;
	LockedEncounterId = 0;
	StopStrikePresentation();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGBossTelegraphedStrikeAbility::StopStrikePresentation()
{
	UAnimMontage* Montage = ExecutionMontage.Get();
	ExecutionMontage.Reset();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	// A death/hit reaction or a subsequent execution owns its own montage.
	if (ASC && Montage && ASC->GetAnimatingAbility() == this && ASC->GetCurrentMontage() == Montage)
	{
		ASC->CurrentMontageStop(.1f);
		ASC->ClearAnimatingAbility(this);
	}
}
