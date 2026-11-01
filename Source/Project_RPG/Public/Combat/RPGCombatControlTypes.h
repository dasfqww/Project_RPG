#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGCombatControlTypes.generated.h"

class AActor;
class UGameplayAbility;
struct FGameplayAbilitySpec;

/** A projection of authoritative GE state for minimal-replication NPC presentation. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGStunPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bActive = false;
	UPROPERTY(BlueprintReadOnly) double EndServerTime = 0.0;
	UPROPERTY() uint32 RecoveryRevision = 0;
	UPROPERTY() uint32 Revision = 0;
};

namespace RPGCombatControl
{
	PROJECT_RPG_API FGameplayTag StunTag();
	PROJECT_RPG_API FGameplayTag StunImmunityTag();
	PROJECT_RPG_API bool IsStunned(const AActor* Actor);
	/** Shared voluntary-action gate; displacement itself remains an owned CMC root-motion source. */
	PROJECT_RPG_API bool IsActionBlocked(const AActor* Actor);
	/** UI/local-only, passive/on-grant and death abilities are outside this control scope. */
	PROJECT_RPG_API bool IsStunAffectedAbility(const UGameplayAbility* Ability);
	/** Zero disables the optional attack effect; positive durations are bounded. */
	PROJECT_RPG_API bool IsValidStunDuration(float Seconds);
	PROJECT_RPG_API bool SetGrantedStunDuration(FGameplayAbilitySpec& Spec, float Seconds);
	PROJECT_RPG_API float GetGrantedStunDuration(const FGameplayAbilitySpec* Spec);
}
