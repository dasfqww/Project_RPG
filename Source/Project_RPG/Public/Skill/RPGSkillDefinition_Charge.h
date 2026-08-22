// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Skill/RPGSkillDefinition.h"
#include "Type/RPGStructTypes.h"
#include "RPGSkillDefinition_Charge.generated.h"

/**
 * URPGSkillDefinition_Charge
 * 
 * 차징 스킬에 필요한 추가 데이터를 정의합니다.
 */
UCLASS()
class PROJECT_RPG_API URPGSkillDefinition_Charge : public URPGSkillDefinition
{
	GENERATED_BODY()
	
public:
	URPGSkillDefinition_Charge();

	UPROPERTY(EditDefaultsOnly, Category = "Charge")
	float MaxChargeHoldTime = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Charge")
	int32 MaxChargeLevel = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Charge")
	float ChargeTimePerLevel = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Charge")
	FMultipleSectionData MontageSections;

	UPROPERTY(EditDefaultsOnly, Category = "Charge")
	TMap<int32, FChargeLevelNiagaraOptionData> ChargeLevelSettings;

protected:
	virtual void ApplyDefinitionExecutionDefaults(
		FRPGSkillRuntimeSpec& OutSpec) const override;
};
