#pragma once
#include "Tests/RPGCombatControlTestFixture.h"
#include "RPGDefenseReductionTestFixture.generated.h"

/** Exposes the unchanged production BP entry for native approved-damage assertions. */
UCLASS(Transient, NotBlueprintable)
class URPGDefenseReductionTestAbility : public URPGStunTestCombatAbility
{
	GENERATED_BODY()
public:
	using URPGGameplayAbility::BP_ApplyEffectSpecHandleToTarget;
};
