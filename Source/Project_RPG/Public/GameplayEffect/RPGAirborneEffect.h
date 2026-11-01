#pragma once
#include "GameplayEffect.h"
#include "RPGAirborneEffect.generated.h"

/** Continuous action gate across phase changes; removed only by its owning execution. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGKnockdownSequenceEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public: URPGKnockdownSequenceEffect();
};
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGAirborneEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public: URPGAirborneEffect();
};
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGDownedEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public: URPGDownedEffect();
};
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGGettingUpEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public: URPGGettingUpEffect();
};
