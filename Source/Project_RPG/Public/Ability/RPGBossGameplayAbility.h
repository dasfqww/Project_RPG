#pragma once

#include "Ability/RPGNPCGameplayAbility.h"
#include "AI/RPGBossAIComponent.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGBossGameplayAbility.generated.h"

class APawn;
class AController;
class UGameplayEffect;
class URPGBossThreatComponent;

/** Interruptible server-only boss action, targeted by default. Timing, commit and presentation remain the concrete ability's job. */
UCLASS(Abstract)
class PROJECT_RPG_API URPGBossGameplayAbility : public URPGNPCGameplayAbility
{
	GENERATED_BODY()
public:
	URPGBossGameplayAbility();

	/** Optional GAS cooldown starting at CommitAbility. Zero preserves the authored legacy cooldown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Ability|Cooldown", meta = (ClampMin = "0.0", ClampMax = "600.0", Units = "s"))
	float PatternCooldownSeconds = 0.f;

	/** Use a unique tag per pattern, or intentionally share one to share cooldowns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Ability|Cooldown")
	FGameplayTag PatternCooldownTag;

	/** Targeted patterns only: inclusive horizontal Pawn-center distance at activation.
	 * Zero disables the lower bound. This is selection policy, not hit reach. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Ability|Activation", meta = (ClampMin = "0.0", ClampMax = "1000000.0", Units = "cm"))
	float MinimumActivationDistance = 0.f;

	/** Zero disables the upper bound. Does not expand the authoritative hit profile. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Ability|Activation", meta = (ClampMin = "0.0", ClampMax = "1000000.0", Units = "cm"))
	float MaximumActivationDistance = 0.f;

	/** Targeted patterns only: use the server controller's normal LOS test before starting.
	 * Target-independent mechanics are exempt. Existing damage/LOS validation still applies at impact. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Ability|Activation")
	bool bRequireActivationLineOfSight = false;

	virtual const FGameplayTagContainer* GetCooldownTags() const override;

	/** Recheck immediately before every authoritative hit/spawn; not a range or LOS test. */
	UFUNCTION(BlueprintPure, Category = "Boss|Ability")
	bool IsBossExecutionValid() const;

	/** Null for target-independent mechanics; never substitutes a new threat target. */
	UFUNCTION(BlueprintPure, Category = "Boss|Ability")
	APawn* GetBossExecutionTarget() const;

	/** Captured encounter, for delayed effects. Never substitute the current encounter at impact. */
	UFUNCTION(BlueprintPure, Category = "Boss|Ability")
	int32 GetBossExecutionEncounterId() const { return EncounterId; }

	uint64 GetBossExecutionSerial() const { return ExecutionSerial; }
	virtual bool CanBeCanceled() const override final { return true; }

protected:
	/** Native opt-in for self/encounter mechanics only. The decision is frozen at
	 * activation; targeted damage helpers never acquire a replacement target. */
	virtual bool RequiresExecutionTarget() const { return true; }

	/** Publish the server-owned windup for this execution; returns zero when rejected. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Ability|Telegraph")
	int32 StartBossTelegraph(FGameplayTag ActionTag, FVector AimLocation, float WindupSeconds);

	/** End this execution's windup early; EndAbility also cleans it up. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Ability|Telegraph")
	bool FinishBossTelegraph();

	/**
	 * Runs one authoritative query and applies one instant damage spec to the captured threat target.
	 * A concrete ability must CommitAbility first and invoke this at its server-owned hit window.
	 * Only one successful damage application is allowed per execution; multi-hit/AOE needs a separate contract.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Ability|Damage")
	bool ExecuteTargetedServerDamage(const FTransform& QueryTransform, const FRPGHitQueryProfile& HitProfile,
		TSubclassOf<UGameplayEffect> DamageEffectClass, const FScalableFloat& BaseDamage,
		const FRPGSkillSecurityProfile& SecurityProfile, FRPGHitQueryResult& OutHit);

	/** One server query per execution; each living threat participant in the area can be hit once.
	 * Commit first. A miss consumes the area strike too. Reset/cancellation/phase change stops the loop.
	 * Killing the aimed target does not spare the other participants in this already-resolved impact. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Ability|Damage")
	int32 ExecuteAreaServerDamage(const FTransform& QueryTransform, const FRPGHitQueryProfile& HitProfile,
		TSubclassOf<UGameplayEffect> DamageEffectClass, const FScalableFloat& BaseDamage,
		const FRPGSkillSecurityProfile& SecurityProfile, TArray<FRPGHitQueryResult>& OutHits);

	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual bool CommitCheck(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) override;
	virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	bool IsBossEncounterExecutionValid() const;
	bool IsPatternCooldownConfigurationValid() const;
	bool PassesActivationTargetConditions(const APawn* Source, const APawn* Target, bool bRequiresTarget) const;
	mutable FGameplayTagContainer CachedPatternCooldownTags;
	TWeakObjectPtr<URPGBossAIComponent> Boss;
	TWeakObjectPtr<URPGBossThreatComponent> Threat;
	TWeakObjectPtr<APawn> BossPawn;
	TWeakObjectPtr<AController> BossController;
	TWeakObjectPtr<APawn> ExecutionTarget;
	TWeakObjectPtr<AController> ExecutionTargetController;
	int32 EncounterId = 0;
	int32 PhaseIndex = INDEX_NONE;
	uint64 ExecutionSerial = 0;
	bool bContextOpen = false;
	bool bRequiresTargetThisExecution = true;
	bool bDamageAppliedThisExecution = false;
	bool bAreaDamageResolvedThisExecution = false;
	bool bApplyingDamage = false;
	int32 TelegraphEncounterId = 0;
	int32 TelegraphSequence = 0;

	UFUNCTION()
	void OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& NewState);
};
