#include "Ability/RPGBossPatternCooldownEffect.h"

URPGBossPatternCooldownEffect::URPGBossPatternCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.f);
}
