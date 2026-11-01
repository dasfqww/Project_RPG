#include "Component/RPGAbilitySystemComponent.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGSkillEndNetworkObservation.h"
#endif

void URPGAbilitySystemComponent::ClientEndAbility_Implementation(const FGameplayAbilitySpecHandle AbilityToEnd,
    const FGameplayAbilityActivationInfo ActivationInfo)
{
#if !UE_BUILD_SHIPPING
    // Only observe the existing authenticated server-to-owner GAS RPC. Do not gate it,
    // call EndAbility ourselves, or change the superclass's prediction-key validation.
    FRPGSkillEndNetworkObservation Receipt(*this, AbilityToEnd, ActivationInfo);
#endif
    Super::ClientEndAbility_Implementation(AbilityToEnd, ActivationInfo);
}
