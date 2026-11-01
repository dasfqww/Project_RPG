#include "GameplayEffect/RPGDefenseReductionEffect.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

URPGDefenseReductionEffect::URPGDefenseReductionEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.f));
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("DefenseReductionTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted; Granted.Added.AddTag(RPGDefenseReduction::StatusTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = URPGAttributeSet::GetDefenseAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyCompound;
	FSetByCallerFloat Magnitude; Magnitude.DataTag = RPGDefenseReduction::MagnitudeTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
	Modifiers.Add(Modifier);
}
