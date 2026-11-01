#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGSlowTypes.generated.h"

struct FGameplayAbilitySpec;

/** Optional approved-hit slow shared by Player/NPC grant and skill data. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSlowSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="90", Units="Percent"))
	float Percent = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.05", ClampMax="30", Units="s"))
	float Duration = 3.f;
	bool IsValid() const;
	bool IsEnabled() const { return Percent > 0.f && IsValid(); }
};

/** Read-only projection for minimal-replication actors, never the speed authority. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSlowPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bActive = false;
	UPROPERTY(BlueprintReadOnly) float Percent = 0.f;
	/** End of the currently strongest slow; a weaker remaining slow may follow. */
	UPROPERTY(BlueprintReadOnly) double EndServerTime = 0.;
	UPROPERTY() uint32 RecoveryRevision = 0;
	UPROPERTY() uint32 Revision = 0;
};

namespace RPGSlow
{
	PROJECT_RPG_API FGameplayTag StatusTag();
	PROJECT_RPG_API FGameplayTag ImmunityTag();
	PROJECT_RPG_API FGameplayTag MagnitudeTag();
	PROJECT_RPG_API bool SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGSlowSettings& Settings);
	PROJECT_RPG_API FRPGSlowSettings GetGrantedSettings(const FGameplayAbilitySpec* Spec);
}
