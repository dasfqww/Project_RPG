#include "GameplayEffect/RPGEquipmentProgressionEffect.h"
#include "Attribute/RPGAttributeSet.h"

URPGEquipmentProgressionEffect::URPGEquipmentProgressionEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	for (const bool bAttack : {true, false})
	{
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = bAttack ? URPGAttributeSet::GetAttackAttribute() : URPGAttributeSet::GetMaxHealthAttribute();
		Modifier.ModifierOp = EGameplayModOp::Additive;
		FSetByCallerFloat Magnitude;
		Magnitude.DataName = bAttack ? AttackMagnitude() : HealthMagnitude();
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
		Modifiers.Add(Modifier);
	}
}
