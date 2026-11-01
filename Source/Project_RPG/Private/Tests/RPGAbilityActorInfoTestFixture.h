#pragma once

#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGAbilityActorInfoTestFixture.generated.h"

/** Isolated native lifecycle observer; never granted to production content. */
UCLASS(Transient, NotBlueprintable)
class URPGAbilityActorInfoTestAbility : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()
public:
	URPGAbilityActorInfoTestAbility();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
	void Finish();
	void BindForTest(const FGameplayAbilityActorInfo* Info);
	const URPGAttributeSet* RefreshForTest(const FGameplayAbilityActorInfo* Info);
	const URPGAttributeSet* CachedAttributes() const { return CachedAttributeSet; }
	bool TryLegacyManaForTest(const FGameplayAbilityActorInfo* Info);
	bool bPreparedAtEntry = false;
	const URPGAttributeSet* ObservedAttributes = nullptr;
	float ObservedAttackSpeed = 0.f;
};

UCLASS(Transient, NotBlueprintable)
class URPGAbilityActorInfoPerActorTestAbility final : public URPGAbilityActorInfoTestAbility
{
	GENERATED_BODY()
public:
	URPGAbilityActorInfoPerActorTestAbility();
};

UCLASS(Transient, NotBlueprintable)
class URPGAbilityActorInfoSpawnTestAbility final : public URPGAbilityActorInfoTestAbility
{
	GENERATED_BODY()
public:
	URPGAbilityActorInfoSpawnTestAbility();
};
