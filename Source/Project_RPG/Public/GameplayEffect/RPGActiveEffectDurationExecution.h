#pragma once

#include "GameplayEffectExecutionCalculation.h"
#include "RPGActiveEffectDurationExecution.generated.h"

/** Applies D1 scoped duration modifiers to every duration-based active effect. */
UCLASS()
class PROJECT_RPG_API URPGActiveEffectDurationExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	URPGActiveEffectDurationExecution();

protected:
	virtual void Execute_Implementation(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
