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

/**
 * Casting starts from one press and resolves automatically after CastDuration.
 * Input does not need to remain held unless cancellation-on-release is authored.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillCastingExecutionConfig
	: public FRPGSkillExecutionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting",
		meta = (ClampMin = "0.01", ClampMax = "60.0", Units = "s"))
	float CastDuration = 1.0f;

	/** Optional. Releasing input before completion cancels this cast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting")
	bool bCancelOnInputRelease = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName CastingSection = NAME_None;

	/** Required section entered when the cast timer completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName CompleteSection = NAME_None;

	/** Optional section entered when the cast is cancelled by input release. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName CancelSection = NAME_None;
};

/**
 * Chain opens a timed input window at each authored Anim Notify.
 * Unlike Combo, a held key does not advance by default; each link expects a
 * deliberate additional press, while an early press may be buffered once.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillChainExecutionConfig
	: public FRPGSkillExecutionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain")
	TArray<FName> ChainSections;

	/**
	 * Event emitted by RPGAnimNotify_SendGameplayEvent to open each link window.
	 * Invalid uses GameplayEvent.Skill.Chain.Window.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain")
	FGameplayTag LinkWindowEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain",
		meta = (ClampMin = "0.05", ClampMax = "5.0", Units = "s"))
	float LinkWindowDuration = 0.75f;

	/** Accept one press made before the authored link-window notify. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain")
	bool bAllowEarlyPressBuffer = true;

	/** Optional accessibility behavior; false preserves discrete chain inputs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain")
	bool bAdvanceWhileInputHeld = false;
};
