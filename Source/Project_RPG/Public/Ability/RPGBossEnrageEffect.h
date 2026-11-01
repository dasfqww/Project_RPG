#pragma once

#include "GameplayEffect.h"
#include "RPGBossEnrageEffect.generated.h"

/** Encounter-owned attack buff. Removal restores other buffs instead of writing an old base value. */
UCLASS()
class PROJECT_RPG_API URPGBossEnrageEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGBossEnrageEffect();
	static FGameplayTag GetMultiplierTag();
};
