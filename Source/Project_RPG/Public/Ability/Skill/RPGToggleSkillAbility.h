// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGToggleSkillAbility.generated.h"

class UAbilityTask_PlayMontageAndWait;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGToggleSkillAbility : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()
public:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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
	//~ End UGameplayAbility Interface
protected:
	virtual void PlaySkillMontage() override;

	void ToggleSkill();

	void UpdateToggleTime();

	void StartSkill();
	void EndOrCancelSkill();

private:
	bool bIsActive=true;

	/*UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayAttackTask;*/

	UPROPERTY(EditDefaultsOnly, Category = "Skill")
	TObjectPtr<UAnimSequence> AnimToPlay;

	UPROPERTY(EditDefaultsOnly, Category = "Skill")
	FMultipleSectionData MultipleSectionData;

	UPROPERTY(EditDefaultsOnly, Category = "Skill")
	float MaxToggleTime = 4.f;

	float CurrentToggleTime;

	float StartTime;

	FTimerHandle ToggleTimerHandle;
};
