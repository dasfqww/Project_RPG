#pragma once
#include "GameplayEffect.h"
#include "GameplayEffectExecutionCalculation.h"
#include "RPGBurnEffect.generated.h"

/** Lifetime/tag only. The server scheduler issues separately approved Instant ticks. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGBurnEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGBurnEffect();
};

UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGBurnTickEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGBurnTickEffect();
};

UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGBurnDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()
public:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& Params,
		FGameplayEffectCustomExecutionOutput& Output) const override;
};
