#pragma once
#include "GameplayEffect.h"
#include "RPGCombatImmunityEffect.generated.h"

/** GAS owns the tags; the activating ability owns this exact effect handle. No expiry timer. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGCombatImmunityEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGCombatImmunityEffect();
};
