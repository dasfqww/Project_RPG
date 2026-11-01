#pragma once

#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "CoreMinimal.h"
#include "Combat/RPGHitReactionTypes.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGBurnTypes.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "Combat/RPGDamageTakenIncreaseTypes.h"
#include "Combat/RPGStaggerTypes.h"
#include "Combat/RPGCombatContributionTypes.h"
#include "Combat/RPGCombatImmunityTypes.h"
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
	/** Resolve the frozen Radius scalar without changing the authored profile or security limits.
	 * Sectors/rings and boxes expand horizontally; capsules retain their proportions;
	 * line sweeps expand their width only. Aim/movement distance and offsets are unchanged.
	 */
	bool TryGetHitQueryProfile(FRPGHitQueryProfile& OutProfile) const;
	/** Opt-in montage translation scalar; a changed scalar requires a bounded server movement budget. */
	bool TryGetMontageMovementScale(float& OutScale) const;
	bool TryGetCombatContributions(FRPGCombatContributionAmounts& OutAmounts) const;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	FGameplayTag SkillTag;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Identity")
	bool bGreatSwordRageSkill = false;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Identity")
	float GreatSwordRageCost = 50.f;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Identity")
	float GreatSwordRageDamageMultiplier = 1.15f;

	/** Presentation name frozen with this activation; never reread mutable selection/definition state. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Presentation")
	FText DisplayName;

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

	/** Frozen sum of the selected, unlocked tripods' flat cooldown reductions. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill")
	float CooldownReductionSeconds = 0.0f;

	/** Server-authored table damage, frozen with the activation's targeting and limits. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Damage")
	float BaseDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Contribution")
	FRPGCombatContributionAmounts Contributions;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Contribution")
	FName ContributionHitSection = NAME_None;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Combat")
	ERPGAttackHitReactionPolicy HitReactionPolicy = ERPGAttackHitReactionPolicy::Legacy;
	UPROPERTY(Transient, BlueprintReadOnly)
	float StunDuration = 0.f;
	UPROPERTY(Transient, BlueprintReadOnly)
	FRPGKnockbackSettings Knockback;
	UPROPERTY(Transient, BlueprintReadOnly)
	FRPGSlowSettings Slow;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Status")
	FRPGBurnSettings Burn;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Status")
	FRPGDefenseReductionSettings DefenseReduction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Status")
	FRPGDamageTakenIncreaseSettings DamageTakenIncrease;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Control")
	FRPGStaggerSettings Stagger;
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FRPGCombatImmunitySettings ActivationImmunities;

	/** Frozen targeting data shared by player skills, PvE attacks, and boss patterns. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Targeting")
	FRPGHitQueryProfile TargetingProfile;

	/** Frozen server constraints resolved from the same definition as this activation. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Skill|Security")
	FRPGSkillSecurityProfile SecurityProfile;
};
