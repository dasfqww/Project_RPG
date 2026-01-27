// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGChargeSkillAbility.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UAbilityTask_PlayMontageAndWait;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnChargeLevelChanged, int32);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGChargeSkillAbility : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()
public:
	URPGChargeSkillAbility();

	//~ Begin UGameplayAbility Interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	
	virtual void CancelAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateCancel) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
	/*virtual void CancelAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, 
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateCancelAbility);*/

	/*virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* 
		ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)override;*/
	
	//~ End UGameplayAbility Interface

protected:
	virtual void PlaySkillMontage() override;

	void ChargeSkill();

	void UpdateChargeTime();

	void OnChargeCompleted();

	void OnOverchargeExpired();

	void HandleChargeLevelChanged(int32 Chargelevel);
	//void PlaySkillMontage() override;


private:
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FMultipleSectionData MultipleSectionData;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraSystem> ChargeNS;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TMap<int32, FChargeLevelNiagaraOptionData> ChargeLevelSettings;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> NiagaraComp;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayAttackTask;

	FTimerHandle ChargeTimerHandle;
	FTimerHandle OverchargeTimerHandle;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	float CanMaxChargeHoldTime = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	int32 MaxChargeLevel = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	float ChargeTimePerLevel = 0.5f;

	int32 CurrentChargeLevel = 0;

	float ChargeTime;

	float StartTime;

	FOnChargeLevelChanged OnChargeLevelChanged;  // 델리게이트 선언
};
