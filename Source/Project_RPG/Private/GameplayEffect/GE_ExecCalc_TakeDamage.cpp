// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayEffect/GE_ExecCalc_TakeDamage.h"
#include "Attribute/RPGAttributeSet.h"
#include "RPGGameplayTags.h"
#include "Character/RPGBaseCharacter.h"
#include "Character/RPGPlayer.h"

#include "RPGDebugHelper.h"

struct FRPGDamageCapture
{
	DECLARE_ATTRIBUTE_CAPTUREDEF(Attack)
	DECLARE_ATTRIBUTE_CAPTUREDEF(Defense)
	DECLARE_ATTRIBUTE_CAPTUREDEF(TakeDamage)
	DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalChance)
	DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalDamage)
	DECLARE_ATTRIBUTE_CAPTUREDEF(IdentityGainMultiplier)
	DECLARE_ATTRIBUTE_CAPTUREDEF(CurrentIdentityGauge)

	FRPGDamageCapture()
	{
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, Attack, Source, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, CriticalChance, Source, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, CriticalDamage, Source, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, IdentityGainMultiplier, Source, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, CurrentIdentityGauge, Source, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, Defense, Target, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, TakeDamage, Target, false)
	}
};

static const FRPGDamageCapture& GetRPGDamageCapture()
{
	static FRPGDamageCapture RPGDamageCapture;
	return RPGDamageCapture;
}

UGE_ExecCalc_TakeDamage::UGE_ExecCalc_TakeDamage()
{
	RelevantAttributesToCapture.Add(GetRPGDamageCapture().AttackDef);
	RelevantAttributesToCapture.Add(GetRPGDamageCapture().CriticalChanceDef);
	RelevantAttributesToCapture.Add(GetRPGDamageCapture().CriticalDamageDef);
	RelevantAttributesToCapture.Add(GetRPGDamageCapture().IdentityGainMultiplierDef);
	RelevantAttributesToCapture.Add(GetRPGDamageCapture().CurrentIdentityGaugeDef);
	RelevantAttributesToCapture.Add(GetRPGDamageCapture().DefenseDef);
	RelevantAttributesToCapture.Add(GetRPGDamageCapture().TakeDamageDef);
}

void UGE_ExecCalc_TakeDamage::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, 
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& EffectSpec = ExecutionParams.GetOwningSpec();

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = EffectSpec.CapturedSourceTags.GetAggregatedTags();
	EvaluateParameters.TargetTags = EffectSpec.CapturedTargetTags.GetAggregatedTags();

	float SourceAttack = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		GetRPGDamageCapture().AttackDef,
		EvaluateParameters,
		SourceAttack
	);

	float SourceCritChance = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		GetRPGDamageCapture().CriticalChanceDef,
		EvaluateParameters,
		SourceCritChance
	);

	float SourceCritDamage= 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		GetRPGDamageCapture().CriticalDamageDef,
		EvaluateParameters,
		SourceCritDamage
	);

	float SourceIdentityMultiplier = 1.0f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		GetRPGDamageCapture().IdentityGainMultiplierDef,
		EvaluateParameters,
		SourceIdentityMultiplier
	);

	float BaseDamage = 0.f;
	float BaseIdentityGain = 0.f;

	for (const TPair<FGameplayTag, float>& TagMagnitude : EffectSpec.SetByCallerTagMagnitudes)
	{
		if (TagMagnitude.Key.MatchesTagExact(RPGGameplayTags::Shared_SetByCaller_BaseDamage))
		{
			BaseDamage = TagMagnitude.Value;
		}

		if (TagMagnitude.Key.MatchesTagExact(RPGGameplayTags::Shared_SetByCaller_IdentityGain))
		{
			BaseIdentityGain = TagMagnitude.Value;
		}
	}

	float TargetDefense = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		GetRPGDamageCapture().DefenseDef, 
		EvaluateParameters,
		TargetDefense
	);

	bool bIsCritical = FMath::RandRange(0.0f, 1.0f) < SourceCritChance;

	int32 FinalDamageDone = 0;

	UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();

	AActor* TargetAvatarActor = TargetASC ? TargetASC->GetAvatarActor() : nullptr;
	AActor* SourceAvatarActor = SourceASC ? SourceASC->GetAvatarActor() : nullptr;

	if (bIsCritical)
	{
		FinalDamageDone = (BaseDamage * SourceAttack * SourceCritDamage) / TargetDefense;
	}
	else
	{
		FinalDamageDone = BaseDamage * SourceAttack / TargetDefense;
	}

	// 아이덴티티 게이지 획득 처리 (공격자가 플레이어인 경우)
	if (BaseIdentityGain > 0.f && SourceASC && SourceAvatarActor->IsA<ARPGPlayer>())
	{
		float FinalIdentityGain = BaseIdentityGain * SourceIdentityMultiplier;
		
		// 소스(공격자)의 아이덴티티 게이지 직접 수정 (Additive)
		SourceASC->ApplyModToAttribute(GetRPGDamageCapture().CurrentIdentityGaugeProperty, EGameplayModOp::Additive, FinalIdentityGain);
		
		//Debug::Print(FString::Printf(TEXT("Identity Gained: %.2f"), FinalIdentityGain), FColor::Cyan);
	}

	if (ARPGBaseCharacter* TargetCharacter=Cast<ARPGBaseCharacter>(TargetAvatarActor))
	{
		if (TargetCharacter->IsPlayerControlled())
		{
			if (ARPGPlayer* Player=Cast<ARPGPlayer>(TargetCharacter))
			{
				FVector Location = Player->GetDamageFontComponent()->GetComponentLocation();
				TargetCharacter->ShowDamageFont(FinalDamageDone, Location, bIsCritical, true);
			}
		}

		else
		{
			TargetCharacter->ShowDamageFont(FinalDamageDone, TargetCharacter->GetActorLocation(), bIsCritical, false);
		}
	}

	
	
	//Debug::Print(TEXT("FinalDamageDone"), FinalDamageDone);

	if (FinalDamageDone>0.f)
	{
		OutExecutionOutput.AddOutputModifier(
			FGameplayModifierEvaluatedData(
				GetRPGDamageCapture().TakeDamageProperty,
				EGameplayModOp::Override,
				FinalDamageDone
			)
		);
	}
}