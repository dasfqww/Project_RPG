#pragma once
#include "GameplayEffect.h"
#include "RPGKnockbackEffect.generated.h"

/** Duration/tag owner for one authoritative knockback execution. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGKnockbackEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGKnockbackEffect();
};
