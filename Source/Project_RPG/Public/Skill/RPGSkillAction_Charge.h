// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Skill/RPGSkillAction.h"
#include "Type/RPGStructTypes.h"
#include "RPGSkillAction_Charge.generated.h"

class UNiagaraComponent;
class UAbilityTask_PlayMontageAndWait;

/**
 * URPGSkillAction_Charge
 * 
 * 차징 로직을 수행하는 액션 클래스입니다.
 */
UCLASS()
class PROJECT_RPG_API URPGSkillAction_Charge : public URPGSkillAction
{
	GENERATED_BODY()

public:
	URPGSkillAction_Charge();

	virtual void StartAction() override;
	virtual void CancelAction() override;
	virtual void EndAction() override;
	virtual void OnInputReleased() override;

protected:
	void UpdateChargeTime();
	void OnChargeCompleted();
	void OnOverchargeExpired();
	void HandleChargeLevelChanged(int32 NewLevel);
	
	void PlayMontageSection(int32 SectionIndex);

private:
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> NiagaraComp;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayAttackTask;

	FTimerHandle ChargeTimerHandle;
	
	int32 CurrentChargeLevel = 0;
	float StartTime;
	float ChargeTime;
};
