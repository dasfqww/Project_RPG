#include "GameplayEffect/RPGEquipmentProgressionEffect.h"
#include "Attribute/RPGAttributeSet.h"

URPGEquipmentProgressionEffect::URPGEquipmentProgressionEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	const auto AddModifier = [this](const FGameplayAttribute& Attribute, FName MagnitudeName)
	{
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		FSetByCallerFloat Magnitude;
		Magnitude.DataName = MagnitudeName;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
		Modifiers.Add(Modifier);
	};
	AddModifier(URPGAttributeSet::GetAttackAttribute(), AttackMagnitude());
	AddModifier(URPGAttributeSet::GetMaxHealthAttribute(), HealthMagnitude());
	AddModifier(URPGAttributeSet::GetCooldownReductionPercentAttribute(), CooldownReductionMagnitude());
	AddModifier(URPGAttributeSet::GetDamageReductionPercentAttribute(), DamageReductionMagnitude());
}
