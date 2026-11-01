#pragma once

#include "GameplayAbilitySpec.h"

class UAbilitySystemComponent;
class URPGGameplayAbility_SkillContainer;

/** Read-only, opt-in provenance of an actual GAS ClientEndAbility RPC stack.
 * No new RPC, synthetic end, montage timing change or production termination policy. */
class FRPGSkillEndNetworkObservation
{
public:
    FRPGSkillEndNetworkObservation(const UAbilitySystemComponent& ASC,
        FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActivationInfo& Activation);
    ~FRPGSkillEndNetworkObservation();
    FRPGSkillEndNetworkObservation(const FRPGSkillEndNetworkObservation&) = delete;
    FRPGSkillEndNetworkObservation& operator=(const FRPGSkillEndNetworkObservation&) = delete;
    static void ObserveEnd(const URPGGameplayAbility_SkillContainer& Skill,
        const UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActivationInfo& Activation, bool bReplicate, bool bCancelled);
private:
#if !UE_BUILD_SHIPPING
    static thread_local FRPGSkillEndNetworkObservation* Current;
    FRPGSkillEndNetworkObservation* Previous = nullptr;
    const UAbilitySystemComponent* SourceASC = nullptr;
    FGameplayAbilitySpecHandle SourceHandle;
    FPredictionKey SourceKey;
    bool bInstalled = false;
#endif
};
