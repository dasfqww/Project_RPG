#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGBurnTypes.generated.h"

struct FGameplayAbilitySpec;

/** Fixed final damage per tick, frozen at the approved hit. Zero disables the status. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBurnSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="10000"))
	int32 TickDamage = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.5", ClampMax="5", Units="s"))
	float Interval = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.5", ClampMax="30", Units="s"))
	float Duration = 5.f;
	bool IsValid() const;
	bool IsEnabled() const { return TickDamage > 0 && IsValid(); }
};

/** Minimal-replication NPCs also expose current status to late observers. No damage authority. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBurnPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Stacks = 0;
	UPROPERTY(BlueprintReadOnly) double EndServerTime = 0.;
	UPROPERTY() uint32 RecoveryRevision = 0;
	UPROPERTY() uint32 Revision = 0;
};

namespace RPGBurn
{
	PROJECT_RPG_API FGameplayTag StatusTag();
	PROJECT_RPG_API FGameplayTag ImmunityTag();
	PROJECT_RPG_API bool SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGBurnSettings& Settings);
	PROJECT_RPG_API FRPGBurnSettings GetGrantedSettings(const FGameplayAbilitySpec* Spec);
}
