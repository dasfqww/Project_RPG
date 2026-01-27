// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGGameplayAbility_SkillContainer.generated.h"

class URPGSkillDefinition;
class URPGSkillAction;

/**
 * URPGGameplayAbility_SkillContainer
 * 
 * 모든 스킬이 공통으로 사용하는 GAS Ability입니다.
 * SkillDefinition을 읽어서 적절한 SkillAction을 실행시키는 "컨테이너" 역할을 합니다.
 */
UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_SkillContainer : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_SkillContainer();

	//~ Begin UGameplayAbility Interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void CancelAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateCancel) override;
	//~ End UGameplayAbility Interface

	// Action에서 작업이 끝났을 때 호출
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void OnActionEnded();

	// Action을 위한 UI/기능 래퍼 (Public)
	void ShowProgressBar_Internal() { ShowProgressBar(GetCurrentActorInfo()); }
	void HiddenProgressBar_Internal() { HiddenProgressBar(GetCurrentActorInfo()); }
	void UpdateProgressBar_Internal(FString TimeText, float Current, float Max) { ShowProcessBarFilling(TimeText, Current, Max); }
	void ProgressCompleted_Internal() { ProgessCompleted(); }
	void ExecuteWaitEvent_Internal() { ExecWaitGameplayEvent(); }

protected:
	// 이 어빌리티가 어떤 스킬인지 정의 (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Skill")
	TObjectPtr<URPGSkillDefinition> SkillDefinition;

	// 현재 실행 중인 액션 인스턴스
	UPROPERTY()
	TObjectPtr<URPGSkillAction> ActiveAction;
};
