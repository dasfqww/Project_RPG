#include "GameplayEffect/RPGStunEffect.h"
#include "Combat/RPGCombatControlTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

URPGStunEffect::URPGStunEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	// UGameplayEffect defaults to None: each approved hit owns an independent GE.
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(RPGCombatControl::StunTag());
	auto* StunTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("StunTags"));
	GEComponents.Add(StunTags);
	StunTags->SetAndApplyTargetTagChanges(Tags);
}
