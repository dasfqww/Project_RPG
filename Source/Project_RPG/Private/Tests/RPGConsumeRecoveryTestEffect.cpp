#include "Tests/RPGConsumeRecoveryTestEffect.h"
#include "Attribute/RPGAttributeSet.h"
#include "RPGItemTags.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGConsumeRecoveryTestEffect)

URPGConsumeRecoveryTestEffect::URPGConsumeRecoveryTestEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FSetByCallerFloat Magnitude;
	Magnitude.DataTag = RPGGameplayTags::Fragment_StatMod_1;
	auto& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = URPGAttributeSet::GetCurrentHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
}
