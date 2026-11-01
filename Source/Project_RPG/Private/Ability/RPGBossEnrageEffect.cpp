#include "Ability/RPGBossEnrageEffect.h"
#include "Attribute/RPGAttributeSet.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossEnrageMultiplier, "Boss.SetByCaller.EnrageAttackMultiplier");

FGameplayTag URPGBossEnrageEffect::GetMultiplierTag()
{
	return TAG_BossEnrageMultiplier;
}

URPGBossEnrageEffect::URPGBossEnrageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	FGameplayModifierInfo Attack;
	Attack.Attribute = URPGAttributeSet::GetAttackAttribute();
	Attack.ModifierOp = EGameplayModOp::MultiplyCompound;
	FSetByCallerFloat Multiplier;
	Multiplier.DataTag = GetMultiplierTag();
	Attack.ModifierMagnitude = FGameplayEffectModifierMagnitude(Multiplier);
	Modifiers.Add(Attack);
}
