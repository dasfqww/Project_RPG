#pragma once

#include "GameplayEffect.h"
#include "RPGStunEffect.generated.h"

/** GE owns each source's lifetime. Overlap grants one aggregate stun until the last removal. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGStunEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGStunEffect();
};
