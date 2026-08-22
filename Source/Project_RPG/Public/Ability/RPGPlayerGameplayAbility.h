// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGGameplayAbility.h"
#include "Security/RPGSecurityTypes.h"
#include "Type/RPGStructTypes.h"
#include "Type/RPGEnumTypes.h"
#include "RPGPlayerGameplayAbility.generated.h"

class ARPGPlayer;
class ARPGPlayerController;
class UPlayerCombatComponent;
class UAbilityTask_WaitGameplayEvent;
class UNiagaraSystem;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGPlayerGameplayAbility : public URPGGameplayAbility
{
	GENERATED_BODY()
public:

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual float GetCooldownTimeRemaining(
		const FGameplayAbilityActorInfo* ActorInfo) const override;
	virtual void GetCooldownTimeRemainingAndDuration(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		float& TimeRemaining,
		float& CooldownDuration) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		ARPGPlayer* GetPlayerCharacterFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		ARPGPlayerController* GetPlayerControllerFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		UPlayerCombatComponent* GetPlayerCombatComponentFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		FGameplayEffectSpecHandle MakePlayerDamageEffectSpecHandle(TSubclassOf<UGameplayEffect> EffectClass, 
				float InWeaponBaseDamage);
	
	UFUNCTION()
	void OnCompleteCallBack();
	
	UFUNCTION()
	void OnInterruptedCallback();

protected:
	/**
	 * Compatibility switch for legacy abilities that mutate mana directly.
	 * New skill containers use GAS Cost GameplayEffects through CommitAbility.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Skill|Legacy")
	bool bUseLegacyManualManaCost = true;

	/** Hard lower bound for the per-skill GAS repeat-input guard. */
	static constexpr float MinimumRepeatCooldown = 1.0f;

	/** Derived skill systems may resolve an authored cooldown above the guard. */
	virtual float GetRPGCooldownDuration(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const;

	/** Stable identity used to keep each skill's cooldown independent. */
	virtual const UObject* GetRPGCooldownSourceObject() const;

	virtual void PlaySkillMontage();

	//float CalculateCriticalDamage(AActor* InCachedTargetActor, float InDamage);

	void ExecWaitGameplayEvent();

	UFUNCTION()
	void OnGameplayEventReceived(FGameplayEventData Payload);

	void LoadSkillData(const FName& SkillRowName);

	void HandleApplyDamage(const FGameplayEventData& InGameplayEventData);
	
	void HandleApplyAOEDamage(const FGameplayEventData& InGameplayEventData);

	FRPGSkillSecurityProfile BuildLegacyDamageSecurityProfile() const;

	void GainIdentity();

	//FGameplayEffectSpecHandle MakeCoolDownGameplayEffect();

	void CoolDown();

	bool ApplyManaCost(const FGameplayAbilityActorInfo* ActorInfo);

	void ShowProgressBar(const FGameplayAbilityActorInfo* ActorInfo);

	void HiddenProgressBar(const FGameplayAbilityActorInfo* ActorInfo);

	void ShowProcessBarFilling(FString& TimeText, float CurremtTime, float RequireTime);

	void ProgessCompleted();

	/*UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	ERPGAttackType AttackType;*/
	
	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> WaitGameplayEventTask;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	EAOETraceType AOETraceType;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraSystem> HitEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> DamageEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> GainIdentityEffectClass;
	
	/*UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> CoolDownEffectClass;*/
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FGameplayTag CurrentAttackTypeTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FGameplayTag EventTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FGameplayTag InputTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FGameplayTag WeaponHitSoundCueTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FGameplayTag HitReactTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FName SkillName;

	/** Compatibility range for legacy overlap-event melee skills. */
	UPROPERTY(EditDefaultsOnly, Category = "Skill|Legacy|Security",
		meta = (ClampMin = "1.0", Units = "cm"))
	float LegacyServerDirectHitDistance = 1200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Skill|Legacy|Security",
		meta = (ClampMin = "0.0", Units = "cm"))
	float LegacyServerHitLocationTolerance = 250.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Skill|Legacy|Security",
		meta = (ClampMin = "1"))
	int32 LegacyMaximumTargetsPerDamageEvent = 32;

	UPROPERTY(EditDefaultsOnly, Category = "Skill|Legacy|Security",
		meta = (ClampMin = "1.0"))
	float LegacyMaximumDamagePerHit = 10000000.0f;

	float RequireManaCost;
	
	float SkillDamage;

	float AttackSpeed;

	float GainIdentityAmount;

private:
	TWeakObjectPtr<ARPGPlayer> CachedPlayer;
	TWeakObjectPtr<ARPGPlayerController> CachedPlayerController;

	
};
