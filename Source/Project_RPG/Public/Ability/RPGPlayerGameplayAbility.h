// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGGameplayAbility.h"
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
	
	virtual void PlaySkillMontage();

	//float CalculateCriticalDamage(AActor* InCachedTargetActor, float InDamage);

	void ExecWaitGameplayEvent();

	UFUNCTION()
	void OnGameplayEventReceived(FGameplayEventData Payload);

	void LoadSkillData(const FName& SkillRowName);

	void HandleApplyDamage(const FGameplayEventData& InGameplayEventData);
	
	void HandleApplyAOEDamage(const FGameplayEventData& InGameplayEventData);

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

	float RequireManaCost;
	
	float SkillDamage;

	float AttackSpeed;

	float GainIdentityAmount;

private:
	TWeakObjectPtr<ARPGPlayer> CachedPlayer;
	TWeakObjectPtr<ARPGPlayerController> CachedPlayerController;

	
};
