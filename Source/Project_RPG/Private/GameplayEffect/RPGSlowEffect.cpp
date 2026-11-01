#include "GameplayEffect/RPGSlowEffect.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGSlowTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
URPGSlowEffect::URPGSlowEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.f));
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("SlowTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted; Granted.Added.AddTag(RPGSlow::StatusTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = URPGAttributeSet::GetMoveSpeedAttribute();
	Modifier.ModifierOp = EGameplayModOp::Multiplicitive;
	FSetByCallerFloat Magnitude; Magnitude.DataTag = RPGSlow::MagnitudeTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
	Modifiers.Add(Modifier);
}
