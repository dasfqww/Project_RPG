// Fill out your copyright notice in the Description page of Project Settings.

#include "GameplayEffect/GE_GainIdentity.h"
#include "Attribute/RPGAttributeSet.h"

UGE_GainIdentity::UGE_GainIdentity()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo IdentityModifier;
	IdentityModifier.Attribute = FGameplayAttribute(
		FindFieldChecked<FProperty>(URPGAttributeSet::StaticClass(),
			GET_MEMBER_NAME_CHECKED(URPGAttributeSet, CurrentRage)));
	IdentityModifier.ModifierOp = EGameplayModOp::Additive;

	FScalableFloat IdentityAmount(0.1f);
	FGameplayEffectModifierMagnitude ModMagnitude(IdentityAmount);

	IdentityModifier.ModifierMagnitude = ModMagnitude;
	Modifiers.Add(IdentityModifier);
}
