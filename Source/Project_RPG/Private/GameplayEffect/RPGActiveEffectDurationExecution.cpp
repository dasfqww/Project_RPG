#include "GameplayEffect/RPGActiveEffectDurationExecution.h"

#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "GameplayEffect.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGActiveEffectDurationExecution)

namespace
{
	struct FRPGActiveDurationStatics
	{
		FRPGActiveDurationStatics()
			: TargetActiveEffectDurationDef(
				URPGAttributeSet::GetActiveEffectDurationAttribute(),
				EGameplayEffectAttributeCaptureSource::Target,
				true)
		{
		}

		FGameplayEffectAttributeCaptureDefinition TargetActiveEffectDurationDef;
	};

	const FRPGActiveDurationStatics& GetActiveDurationStatics()
	{
		static FRPGActiveDurationStatics Statics;
		return Statics;
	}
}

URPGActiveEffectDurationExecution::URPGActiveEffectDurationExecution()
{
	RelevantAttributesToCapture.Add(GetActiveDurationStatics().TargetActiveEffectDurationDef);
}

void URPGActiveEffectDurationExecution::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
#if WITH_SERVER_CODE
	URPGAbilitySystemComponent* TargetASC = Cast<URPGAbilitySystemComponent>(
		ExecutionParams.GetTargetAbilitySystemComponent());
	if (!TargetASC)
	{
		return;
	}

	FAggregatorEvaluateParameters EvaluateParameters;
	for (const FActiveGameplayEffectHandle Handle : TargetASC->GetAllActiveEffectHandles())
	{
		FActiveGameplayEffect* ActiveEffect = TargetASC->GetActiveGameplayEffectMutable(Handle);
		if (!ActiveEffect || ActiveEffect->Spec.Def->DurationPolicy != EGameplayEffectDurationType::HasDuration)
		{
			continue;
		}

		FGameplayTagContainer EffectTags;
		ActiveEffect->Spec.GetAllAssetTags(EffectTags);
		EvaluateParameters.TargetTags = &EffectTags;

		float NewDuration = ActiveEffect->GetDuration();
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitudeWithBase(
			GetActiveDurationStatics().TargetActiveEffectDurationDef,
			EvaluateParameters,
			ActiveEffect->GetDuration(),
			NewDuration);

		ActiveEffect->Spec.Duration = FMath::Max(NewDuration, SMALL_NUMBER);
		TargetASC->MarkActiveGameplayEffectDirty(ActiveEffect);
		TargetASC->CheckActiveEffectDuration(Handle);
	}
#endif
}
