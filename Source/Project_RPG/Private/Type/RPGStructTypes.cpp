// Fill out your copyright notice in the Description page of Project Settings.


#include "Type/RPGStructTypes.h"
#include "Ability/RPGGameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "Combat/RPGCombatControlTypes.h"

bool FRPGPlayerAbilitySet::IsValid() const
{
	return InputTag.IsValid() && AbilityToGrant && RPGHitReaction::IsValidPolicy(HitReactionPolicy)
		&& RPGCombatControl::IsValidStunDuration(StunDuration) && Knockback.IsValid() && Slow.IsValid() && Burn.IsValid() && DefenseReduction.IsValid() && DamageTakenIncrease.IsValid() && Stagger.IsValid() && ActivationImmunities.IsValid()
		&& (!ActivationImmunities.IsEnabled() || AbilityToGrant->GetDefaultObject<URPGGameplayAbility>()->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced);
}

bool FRPGPlayerAbilitySet::BuildAbilitySpec(int32 Level, UObject* SourceObject, FGameplayAbilitySpec& OutSpec) const
{
	if (!IsValid() || Level < 1) return false;
	OutSpec = FGameplayAbilitySpec(AbilityToGrant, Level);
	OutSpec.SourceObject = SourceObject;
	OutSpec.GetDynamicSpecSourceTags().AddTag(InputTag);
	return RPGHitReaction::SetGrantedPolicy(OutSpec, HitReactionPolicy) && RPGCombatControl::SetGrantedStunDuration(OutSpec, StunDuration)
		&& RPGKnockback::SetGrantedSettings(OutSpec, Knockback) && RPGSlow::SetGrantedSettings(OutSpec, Slow) && RPGBurn::SetGrantedSettings(OutSpec, Burn) && RPGDefenseReduction::SetGrantedSettings(OutSpec, DefenseReduction) && RPGDamageTakenIncrease::SetGrantedSettings(OutSpec, DamageTakenIncrease) && RPGStagger::SetGrantedSettings(OutSpec, Stagger)
		&& RPGCombatImmunity::SetGrantedSettings(OutSpec, ActivationImmunities);
}
