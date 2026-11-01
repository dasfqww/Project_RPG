#pragma once

#include "Tests/RPGBossAbilityTestFixture.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "GameplayEffect.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGBossDamageTestFixture.generated.h"

class UAbilitySystemComponent;

/** Uses the production damage execution and AttributeSet, without requiring a content asset. */
UCLASS(Transient, NotBlueprintable)
class URPGBossTestDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGBossTestDamageEffect();
};

UCLASS(Transient, NotBlueprintable)
class URPGBossTestDurationDamageEffect : public URPGBossTestDamageEffect
{
	GENERATED_BODY()
public:
	URPGBossTestDurationDamageEffect();
};

UCLASS(Transient, NotBlueprintable)
class URPGBossAttackTestProbe : public URPGBossAbilityTestProbe
{
	GENERATED_BODY()
public:
	URPGBossAttackTestProbe();
	FRPGHitQueryProfile HitProfile;
	FRPGSkillSecurityProfile SecurityProfile;
	FScalableFloat Damage;
	TSubclassOf<UGameplayEffect> DamageEffectClass;
	bool bFirstHitApplied = false;
	bool bDuplicateHitApplied = false;
	bool bCommitted = false;
	bool bDeferHit = false;
	bool bAreaAttack = false;
	int32 AreaHitCount = 0;
	TArray<FRPGHitQueryResult> AreaHits;
	bool bTelegraphStarted = false;
	bool bTelegraphCleared = false;
	TWeakObjectPtr<AActor> HitActor;
};

/** Synchronous test attack: commit, query, damage once, verify duplicate suppression, end. */
UCLASS(Transient, NotBlueprintable)
class URPGBossAttackTestAbility : public URPGBossGameplayAbility
{
	GENERATED_BODY()
public:
	bool AttemptTestHit(URPGBossAttackTestProbe* Probe, FRPGHitQueryResult& OutHit);
	int32 AttemptTestAreaHit(URPGBossAttackTestProbe* Probe, TArray<FRPGHitQueryResult>& OutHits);
protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

FGameplayEffectSpecHandle MakeBossTestDamageSpec(UAbilitySystemComponent* SourceASC, float Damage);
