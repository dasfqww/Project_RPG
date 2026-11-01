#pragma once

#include "CoreMinimal.h"
#include "RPGCombatContributionTypes.generated.h"

/** Mechanic units, independent from HP damage and short hit-stun duration. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCombatContributionAmounts
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1000"))
	float Stagger = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1000"))
	float PartBreak = 0.f;
	bool IsValid() const
	{
		return FMath::IsFinite(Stagger) && Stagger >= 0.f && Stagger <= 1000.f
			&& FMath::IsFinite(PartBreak) && PartBreak >= 0.f && PartBreak <= 1000.f;
	}
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCombatContributionReceiverProfile
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bEnabled = false;
	/** Zero disables that mechanic for this target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="100000"))
	float StaggerThreshold = 100.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="100000"))
	float PartBreakThreshold = 100.f;
	bool IsValid() const
	{
		return FMath::IsFinite(StaggerThreshold) && StaggerThreshold >= 0.f && StaggerThreshold <= 100000.f
			&& FMath::IsFinite(PartBreakThreshold) && PartBreakThreshold >= 0.f && PartBreakThreshold <= 100000.f
			&& (!bEnabled || StaggerThreshold > 0.f || PartBreakThreshold > 0.f);
	}
};

/** Read-only replicated receiver state; completion does not authorize damage or interrupt an ability. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCombatContributionState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly) float Stagger = 0.f;
	UPROPERTY(BlueprintReadOnly) float PartBreak = 0.f;
	UPROPERTY(BlueprintReadOnly) float StaggerThreshold = 0.f;
	UPROPERTY(BlueprintReadOnly) float PartBreakThreshold = 0.f;
	UPROPERTY(BlueprintReadOnly) bool bStaggerComplete = false;
	UPROPERTY(BlueprintReadOnly) bool bPartBroken = false;
	UPROPERTY() uint32 Generation = 0;
	UPROPERTY() uint32 Revision = 0;
};
