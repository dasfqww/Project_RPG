#include "Tests/RPGBossDamageTestFixture.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect/GE_ExecCalc_TakeDamage.h"
#include "RPGGameplayTags.h"
#include "NativeGameplayTags.h"
#include "AI/RPGBossAIComponent.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossAttackTestTelegraph, "Tests.RaidBoss.AttackAbility.Telegraph");

URPGBossTestDamageEffect::URPGBossTestDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UGE_ExecCalc_TakeDamage::StaticClass();
	Executions.Add(Execution);
}

URPGBossTestDurationDamageEffect::URPGBossTestDurationDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
}

URPGBossAttackTestProbe::URPGBossAttackTestProbe()
{
	HitProfile.Shape.Type = ERPGHitQueryShape::Sphere;
	HitProfile.Shape.Radius = 200.f;
	HitProfile.Filter.TeamRule = ERPGHitQueryTeamRule::HostileOnly;
	HitProfile.Filter.MaxResults = 8;
	Damage.Value = 31.f;
	DamageEffectClass = URPGBossTestDamageEffect::StaticClass();
}

bool URPGBossAttackTestAbility::AttemptTestHit(URPGBossAttackTestProbe* Probe, FRPGHitQueryResult& OutHit)
{
	return ExecuteTargetedServerDamage(GetAvatarActorFromActorInfo()->GetActorTransform(),
		Probe->HitProfile, Probe->DamageEffectClass, Probe->Damage, Probe->SecurityProfile, OutHit);
}

int32 URPGBossAttackTestAbility::AttemptTestAreaHit(URPGBossAttackTestProbe* Probe, TArray<FRPGHitQueryResult>& OutHits)
{
	return ExecuteAreaServerDamage(GetAvatarActorFromActorInfo()->GetActorTransform(),
		Probe->HitProfile, Probe->DamageEffectClass, Probe->Damage, Probe->SecurityProfile, OutHits);
}

void URPGBossAttackTestAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive() || !IsBossExecutionValid()) { return; }
	URPGBossAttackTestProbe* Probe = CastChecked<URPGBossAttackTestProbe>(GetSourceObject(Handle, ActorInfo));
	++Probe->Activated;
	Probe->SeenTarget = GetBossExecutionTarget();
	Probe->bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo);
	Probe->bTelegraphStarted = Probe->bCommitted && StartBossTelegraph(
		TAG_BossAttackTestTelegraph, Probe->SeenTarget->GetActorLocation(), 2.f) != 0;
	if (Probe->bDeferHit && Probe->bTelegraphStarted) { return; }
	if (Probe->bAreaAttack)
	{
		Probe->AreaHitCount = Probe->bCommitted ? AttemptTestAreaHit(Probe, Probe->AreaHits) : 0;
		Probe->bFirstHitApplied = Probe->AreaHitCount > 0;
		TArray<FRPGHitQueryResult> DuplicateHits;
		Probe->bDuplicateHitApplied = AttemptTestAreaHit(Probe, DuplicateHits) > 0;
		if (IsActive()) { EndAbility(Handle, ActorInfo, ActivationInfo, true, !Probe->bFirstHitApplied); }
		return;
	}
	FRPGHitQueryResult Hit;
	Probe->bFirstHitApplied = Probe->bCommitted && AttemptTestHit(Probe, Hit);
	Probe->HitActor = Hit.TargetActor;
	FRPGHitQueryResult DuplicateHit;
	Probe->bDuplicateHitApplied = AttemptTestHit(Probe, DuplicateHit);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !Probe->bFirstHitApplied);
	if (const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr)
	{
		const URPGBossAIComponent* BossStateComponent = Avatar->FindComponentByClass<URPGBossAIComponent>();
		Probe->bTelegraphCleared = BossStateComponent && !BossStateComponent->GetTelegraphState().bActive;
	}
}

FGameplayEffectSpecHandle MakeBossTestDamageSpec(UAbilitySystemComponent* SourceASC, float Damage)
{
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(URPGBossTestDamageEffect::StaticClass(), 1.f, SourceASC->MakeEffectContext());
	Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, Damage);
	Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_IdentityGain, 0.f);
	return Spec;
}
