// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGComboSkillAbility.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGComboSkillAbility : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()
public:
	URPGComboSkillAbility();

	//~ Begin UGameplayAbility Interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	//~ End UGameplayAbility Interface

	virtual void PlaySkillMontage() override;

	//virtual void HandleApplyDamage(const FGameplayEventData& InGameplayEventData);

	void ResetComboCount();

private:
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FMultipleSectionData DefaultAttackSectionData;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FMultipleSectionData DefaultAttackSectionData_Rage;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> GainIdentityEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	int32 InitComboCount = 1;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	float TimeToTimerReset = 0.5f;

	int32 CurrentComboCount;

	int32 UsedComboCount = 0;

	FTimerHandle ComboCountResetTimerHandle;
};
