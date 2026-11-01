#pragma once
#include "GameplayEffect.h"
#include "RPGSlowEffect.generated.h"

/** Each approved hit owns a duration; the MoveSpeed aggregator selects the strongest reduction. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGSlowEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public: URPGSlowEffect();
};
