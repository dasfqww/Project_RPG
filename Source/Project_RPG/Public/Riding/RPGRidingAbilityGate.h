#pragma once
#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class AActor;

/** One input stream per ASC/avatar. Suppression survives dismount until release, never queues an attack. */
class PROJECT_RPG_API FRPGRidingAbilityGate
{
public:
    static bool IsManualAbility(const UGameplayAbility* Ability);
    static bool BlocksMounting(const UGameplayAbility* Ability);
    static bool IsBlocked(const AActor* Avatar, const UGameplayAbility* Ability);
    bool ConsumeTagPress(UAbilitySystemComponent& ASC, const FGameplayTag& Input);
    bool ConsumeSpecPress(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle);
    void ReleaseTag(UAbilitySystemComponent& ASC, const FGameplayTag& Input);
    void ReleaseSpec(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle);
    void Reset();
private:
    void Synchronize(UAbilitySystemComponent& ASC);
    TWeakObjectPtr<AActor> Avatar;
    FGuid Epoch;
    TSet<FGameplayAbilitySpecHandle> SuppressedSpecs;
    FGameplayTagContainer SuppressedTags;
};
