#include "Tests/RPGDungeonGameplayRevocationTestFixture.h"

URPGDungeonRevocationTestAbility::URPGDungeonRevocationTestAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void URPGDungeonRevocationTestAbility::ActivateAbility(FGameplayAbilitySpecHandle,
	const FGameplayAbilityActorInfo*, FGameplayAbilityActivationInfo, const FGameplayEventData*)
{
	// Intentionally stays active until server cancellation, including non-cancelable state.
}
