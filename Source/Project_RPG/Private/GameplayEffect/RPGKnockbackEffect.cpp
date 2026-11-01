#include "GameplayEffect/RPGKnockbackEffect.h"
#include "Combat/RPGKnockbackTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

URPGKnockbackEffect::URPGKnockbackEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(.4f));
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("KnockbackTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted;
	Granted.Added.AddTag(RPGKnockback::StatusTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
}
