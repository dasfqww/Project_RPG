#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGSkillExecutionTypes.generated.h"

/** Marker base for policy-specific data stored in an FInstancedStruct. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillExecutionConfig
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillInstantExecutionConfig
	: public FRPGSkillExecutionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName StartSection = NAME_None;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillChargeExecutionConfig
	: public FRPGSkillExecutionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.01", Units = "s"))
	float ChargeTimePerLevel = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "1"))
	int32 MaxChargeLevel = 3;

	/** Time the skill may remain at maximum charge before it auto-releases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.0", Units = "s"))
	float MaxChargeHoldTime = 2.0f;

	/** Releasing below this level cancels instead of executing. Zero allows partial attacks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0"))
	int32 MinimumReleaseLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName ChargeSection = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName ReleaseSection = NAME_None;
};

/**
 * Holding reaches a success window over time.
 * Releasing before the perfect zone follows FailureSection or cancels the skill.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillHoldingExecutionConfig
	: public FRPGSkillExecutionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holding",
		meta = (ClampMin = "0.01", Units = "s"))
	float HoldDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holding",
		meta = (ClampMin = "0.0", Units = "s"))
	float PerfectZoneStartTime = 0.8f;

	/** Zero uses HoldDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holding",
		meta = (ClampMin = "0.0", Units = "s"))
	float PerfectZoneEndTime = 0.0f;

	/** Completes successfully at the end of the perfect zone while input remains held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holding")
	bool bAutoReleaseAtPerfectZoneEnd = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName HoldingSection = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName SuccessSection = NAME_None;

	/** Optional. An early release cancels immediately when this is None. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName FailureSection = NAME_None;
};

/**
 * A combo advances only at authored Anim Notify windows.
 * Holding the input continuously advances; repeated presses can also buffer one step.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillComboExecutionConfig
	: public FRPGSkillExecutionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
	TArray<FName> ComboSections;

	/**
	 * Event emitted by RPGAnimNotify_SendGameplayEvent at each advance window.
	 * Invalid uses GameplayEvent.Skill.Combo.Advance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
	FGameplayTag AdvanceEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
	bool bAdvanceWhileInputHeld = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
	bool bAllowRepeatedPressBuffer = true;
};
