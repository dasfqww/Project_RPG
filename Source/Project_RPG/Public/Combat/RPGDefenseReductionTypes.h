#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGDefenseReductionTypes.generated.h"

struct FGameplayAbilitySpec;

/** Independent duration effects compound against current Defense. Zero disables the grant. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDefenseReductionSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="80"))
	float Percent = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.05", ClampMax="30", Units="s"))
	float Duration = 3.f;
	bool IsValid() const;
	bool IsEnabled() const { return Percent > 0.f && IsValid(); }
};

namespace RPGDefenseReduction
{
	PROJECT_RPG_API FGameplayTag StatusTag();
	PROJECT_RPG_API FGameplayTag ImmunityTag();
	PROJECT_RPG_API FGameplayTag MagnitudeTag();
	PROJECT_RPG_API bool SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGDefenseReductionSettings& Settings);
	PROJECT_RPG_API FRPGDefenseReductionSettings GetGrantedSettings(const FGameplayAbilitySpec* Spec);
}
