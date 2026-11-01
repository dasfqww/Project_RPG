#include "GameplayEffect/RPGAirborneEffect.h"
#include "Combat/RPGAirborneTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

URPGKnockdownSequenceEffect::URPGKnockdownSequenceEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("SequenceTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted; Granted.Added.AddTag(RPGAirborne::SequenceTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
}
URPGAirborneEffect::URPGAirborneEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("AirborneTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted; Granted.Added.AddTag(RPGAirborne::AirborneTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
}
URPGDownedEffect::URPGDownedEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(.8f));
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("DownedTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted; Granted.Added.AddTag(RPGAirborne::DownedTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
}
URPGGettingUpEffect::URPGGettingUpEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.2f));
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GettingUpTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted; Granted.Added.AddTag(RPGAirborne::GettingUpTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
}
