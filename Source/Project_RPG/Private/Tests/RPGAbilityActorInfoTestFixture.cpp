#include "Tests/RPGAbilityActorInfoTestFixture.h"

#include "GameplayAbilitySpec.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGAbilityActorInfoTestFixture)

URPGAbilityActorInfoTestAbility::URPGAbilityActorInfoTestAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	bUseLegacyManualManaCost = false;
	bCountTowardServerActivationRateLimit = false;
}

void URPGAbilityActorInfoTestAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	bPreparedAtEntry = CachedAttributeSet != nullptr;
	Super::ActivateAbility(Handle, Info, Activation, Event);
	ObservedAttributes = CachedAttributeSet;
	ObservedAttackSpeed = AttackSpeed;
#else
	EndAbility(Handle, Info, Activation, false, true);
#endif
}

void URPGAbilityActorInfoTestAbility::Finish()
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
#endif
}

void URPGAbilityActorInfoTestAbility::BindForTest(const FGameplayAbilityActorInfo* Info)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	SetCurrentActorInfo(FGameplayAbilitySpecHandle(), Info);
#endif
}

const URPGAttributeSet* URPGAbilityActorInfoTestAbility::RefreshForTest(const FGameplayAbilityActorInfo* Info)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return RefreshAttributeSetFromActorInfo(Info);
#else
	return nullptr;
#endif
}

bool URPGAbilityActorInfoTestAbility::TryLegacyManaForTest(const FGameplayAbilityActorInfo* Info)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	TGuardValue<bool> LegacyCost(bUseLegacyManualManaCost, true);
	return ApplyManaCost(Info);
#else
	return false;
#endif
}

URPGAbilityActorInfoPerActorTestAbility::URPGAbilityActorInfoPerActorTestAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

URPGAbilityActorInfoSpawnTestAbility::URPGAbilityActorInfoSpawnTestAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	ActivationPolicy = ERPGGladiatorAbilityActivationPolicy::OnSpawn;
}
