// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGInstantSkillAbility.generated.h"



/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInstantSkillAbility : public URPGPlayerGameplayAbility
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
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	//~ End UGameplayAbility Interface
	
	virtual void PlaySkillMontage() override;

	//virtual void HandleApplyDamage(const FGameplayEventData& InGameplayEventData) override;

	

private:
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess="true"))
	FSingleSectionData SingleSectionData;
};
