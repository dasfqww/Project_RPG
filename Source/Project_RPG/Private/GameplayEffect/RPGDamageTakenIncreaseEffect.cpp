#include "GameplayEffect/RPGDamageTakenIncreaseEffect.h"
#include "Combat/RPGDamageTakenIncreaseTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

URPGDamageTakenIncreaseEffect::URPGDamageTakenIncreaseEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.f));
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("DamageTakenIncreaseTags"));
	GEComponents.Add(Tags);
	FInheritedTagContainer Granted; Granted.Added.AddTag(RPGDamageTakenIncrease::StatusTag());
	Tags->SetAndApplyTargetTagChanges(Granted);
}
