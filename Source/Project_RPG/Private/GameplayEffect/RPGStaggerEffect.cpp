#include "GameplayEffect/RPGStaggerEffect.h"
#include "Combat/RPGStaggerTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

URPGStaggerEffect::URPGStaggerEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(.3f);
	FInheritedTagContainer Changes; Changes.Added.AddTag(RPGStagger::StatusTag());
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("StaggerStatusTags"));
	Tags->SetAndApplyTargetTagChanges(Changes); GEComponents.Add(Tags);
}
