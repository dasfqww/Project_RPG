#include "Ability/RPGBossPersistentHazardAbility.h"

#include "AI/RPGBossPersistentHazard.h"
#include "AI/RPGBossThreatComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"

URPGBossPersistentHazardAbility::URPGBossPersistentHazardAbility()
{
	HazardClass = ARPGBossPersistentHazard::StaticClass();
}

bool URPGBossPersistentHazardAbility::IsTargetInRange(const APawn* Source, const APawn* Target) const
{
	if (!IsValid(Source) || !IsValid(Target)) { return false; }
	FRPGCombatHitSecuritySample Sample;
	Sample.SourceLocation = Source->GetActorLocation();
	Target->GetActorBounds(true, Sample.TargetBoundsOrigin, Sample.TargetBoundsExtent, false);
	Sample.ImpactPoint = Sample.TargetBoundsOrigin;
	Sample.MaximumDistance = SecurityProfile.MaximumServerHitDistance;
	Sample.HitLocationTolerance = 0.f;
	return FRPGSecurityValidationMath::ValidateCombatHit(Sample).bValid;
}

bool URPGBossPersistentHazardAbility::HasHazardCapacity(const APawn* Source) const
{
	if (!IsValid(Source) || !Source->GetWorld() || MaximumActiveHazards < 1 || MaximumActiveHazards > 16) { return false; }
	int32 Count = 0;
	for (TActorIterator<ARPGBossPersistentHazard> It(Source->GetWorld()); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->GetHazardState().SourcePawn == Source && ++Count >= MaximumActiveHazards) { return false; }
	}
	return true;
}

bool URPGBossPersistentHazardAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const APawn* Avatar = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	const URPGBossAIComponent* BossStateComponent = Avatar ? Avatar->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const URPGBossThreatComponent* Targets = Avatar ? Avatar->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	const UGameplayEffect* Effect = DamageEffectClass ? DamageEffectClass->GetDefaultObject<UGameplayEffect>() : nullptr;
	const float Damage = BaseDamage.GetValueAtLevel(GetAbilityLevel());
	FString Error;
	return BossStateComponent && !BossStateComponent->GetTelegraphState().bActive && ActionTag.IsValid() &&
		HazardClass && !HazardClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) &&
		HazardClass->GetDefaultObject<ARPGBossPersistentHazard>()->GetIsReplicated() &&
		FMath::IsFinite(WindupSeconds) && WindupSeconds >= .01f && WindupSeconds <= 120.f &&
		FMath::IsFinite(Radius) && Radius > 0.f && Radius <= 2500.f &&
		ARPGBossPersistentHazard::IsTimingValid(DurationSeconds, PulseInterval) &&
		FMath::CeilToInt(DurationSeconds / PulseInterval) <= SecurityProfile.MaximumHitsPerActivation &&
		SecurityProfile.bRequireAuthorityHitQuery && SecurityProfile.IsValid(&Error) &&
		FMath::IsFinite(Damage) && Damage > 0.f && Damage <= SecurityProfile.MaximumDamagePerHit &&
		Effect && Effect->DurationPolicy == EGameplayEffectDurationType::Instant && Effect->Period.GetValueAtLevel(1.f) == 0.f &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) &&
		IsValid(Targets) && IsTargetInRange(Avatar, Targets->GetCurrentTarget()) && HasHazardCapacity(Avatar);
}

void URPGBossPersistentHazardAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive() || !IsBossExecutionValid()) { return; }
	const uint64 StartedSerial = GetBossExecutionSerial();
	APawn* Source = Cast<APawn>(GetAvatarActorFromActorInfo());
	APawn* Target = GetBossExecutionTarget();
	const bool bCommitted = IsTargetInRange(Source, Target) && HasHazardCapacity(Source) && CommitAbility(Handle, ActorInfo, ActivationInfo);
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	if (!bCommitted || !IsBossExecutionValid() || !IsTargetInRange(Source, Target))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return;
	}
	LockedCenter = Target->GetActorLocation();
	const int32 Sequence = StartBossTelegraph(ActionTag, LockedCenter, WindupSeconds);
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	LockedSequence = Sequence;
	if (Sequence == 0) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	GetWorld()->GetTimerManager().SetTimer(CastTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, StartedSerial]() { ReleaseHazard(StartedSerial); }), WindupSeconds, false);
}

void URPGBossPersistentHazardAbility::ReleaseHazard(uint64 StartedSerial)
{
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	APawn* Source = Cast<APawn>(GetAvatarActorFromActorInfo());
	const URPGBossAIComponent* BossStateComponent = Source ? Source->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const FRPGBossTelegraphState Telegraph = BossStateComponent ? BossStateComponent->GetTelegraphState() : FRPGBossTelegraphState();
	if (!IsBossExecutionValid() || !HasHazardCapacity(Source) || !Telegraph.bActive ||
		Telegraph.EncounterId != GetBossExecutionEncounterId() || Telegraph.Sequence != LockedSequence || !FinishBossTelegraph())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return;
	}
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	if (!IsBossExecutionValid()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	if (!DamageEffectClass) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	const FGameplayEffectSpecHandle Damage = MakeEnemyDamageEffectSpecHandle(DamageEffectClass, BaseDamage);
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	if (!IsBossExecutionValid()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	ARPGBossPersistentHazard* Created = GetWorld()->SpawnActorDeferred<ARPGBossPersistentHazard>(HazardClass,
		FTransform(LockedCenter), Source, Source, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	PendingHazard = Created;
	if (!Created || !Created->InitializeFromCast(this, LockedCenter, LockedSequence, Damage))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return;
	}
	Created->FinishSpawning(FTransform(LockedCenter));
	if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
	const bool bReleased = IsValid(Created) && !Created->IsActorBeingDestroyed();
	// Handoff complete: the area's captured encounter owns it, not this reusable Ability instance.
	PendingHazard.Reset();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bReleased);
}

void URPGBossPersistentHazardAbility::EndAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) { return; }
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CastTimer); }
	ARPGBossPersistentHazard* Pending = PendingHazard.Get();
	PendingHazard.Reset();
	LockedSequence = 0;
	if (IsValid(Pending)) { Pending->Destroy(); }
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
