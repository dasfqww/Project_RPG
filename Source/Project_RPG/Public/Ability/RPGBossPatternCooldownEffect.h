#pragma once

#include "GameplayEffect.h"
#include "RPGBossPatternCooldownEffect.generated.h"

/** Duration and granted cooldown tag are supplied by the committing server ability. */
UCLASS()
class PROJECT_RPG_API URPGBossPatternCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGBossPatternCooldownEffect();
};
