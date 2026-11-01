#include "GameplayEffect/RPGBurnEffect.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGBurnTypes.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Security/RPGBurnTickGuard.h"

URPGBurnEffect::URPGBurnEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(5.f);
	auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("BurnStatusTags"));
	FInheritedTagContainer Changes; Changes.Added.AddTag(RPGBurn::StatusTag());
	Tags->SetAndApplyTargetTagChanges(Changes); GEComponents.Add(Tags);
}
URPGBurnTickEffect::URPGBurnTickEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution; Execution.CalculationClass = URPGBurnDamageExecution::StaticClass();
	Executions.Add(Execution);
}
void URPGBurnDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& Params,
	FGameplayEffectCustomExecutionOutput& Output) const
{
	const int32 Damage = FRPGBurnTickGuard::Consume(Params.GetOwningSpec(), Params.GetSourceAbilitySystemComponent(), Params.GetTargetAbilitySystemComponent());
	if (Damage > 0) Output.AddOutputModifier(FGameplayModifierEvaluatedData(URPGAttributeSet::GetTakeDamageAttribute(), EGameplayModOp::Override, float(Damage)));
}
