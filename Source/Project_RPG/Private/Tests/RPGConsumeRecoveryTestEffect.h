#pragma once
#include "GameplayEffect.h"
#include "RPGConsumeRecoveryTestEffect.generated.h"

/** Isolated instant heal for native consume recovery fixtures; never assigned to game content. */
UCLASS(Transient, NotBlueprintable)
class URPGConsumeRecoveryTestEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGConsumeRecoveryTestEffect();
};
