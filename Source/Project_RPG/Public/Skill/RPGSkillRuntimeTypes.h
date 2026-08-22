#pragma once

#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Security/RPGSecurityTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "RPGSkillRuntimeTypes.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class URPGSkillAction;
class URPGSkillExecutionPolicy;
class URPGSkillTargetingPolicy;
class UTexture2D;

/** Immutable configuration resolved once for a single skill activation. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillRuntimeSpec
{
	GENERATED_BODY()

	void Reset();
	float GetStatScalar(const FGameplayTag& StatTag, float DefaultValue = 1.0f) const;
	bool HasTripodTag(const FGameplayTag& TripodTag) const;
	float GetCooldownDuration(float MinimumDuration = 1.0f) const;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	FGameplayTag SkillTag;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	int32 SkillLevel = 1;

	/** Invalid or locked selections are normalized to INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	TArray<int32> SelectedTripodIndices;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	FGameplayTagContainer TripodTags;

	/** Multipliers with the same tag are composed multiplicatively. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	TMap<FGameplayTag, float> StatScalars;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	TObjectPtr<UNiagaraSystem> VFX = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	TSubclassOf<URPGSkillAction> ActionClass;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	TSubclassOf<URPGSkillExecutionPolicy> ExecutionPolicyClass;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	FInstancedStruct ExecutionConfig;

	/** Input/aim strategy is independent from the skill execution strategy. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Targeting")
	TSubclassOf<URPGSkillTargetingPolicy> TargetingPolicyClass;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Targeting")
	FInstancedStruct TargetingConfig;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	float BaseCooldown = 1.0f;

	/** Frozen targeting data shared by player skills, PvE attacks, and boss patterns. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Targeting")
	FRPGHitQueryProfile TargetingProfile;

	/** Frozen server constraints resolved from the same definition as this activation. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Security")
	FRPGSkillSecurityProfile SecurityProfile;
};
