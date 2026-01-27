// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GE_ExecCalc_TakeDamage.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UGE_ExecCalc_TakeDamage : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()
public:
	UGE_ExecCalc_TakeDamage();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, 
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
