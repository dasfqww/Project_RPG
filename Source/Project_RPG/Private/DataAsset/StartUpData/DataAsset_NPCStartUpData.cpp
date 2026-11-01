// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "Combat/RPGCombatControlTypes.h"

void UDataAsset_NPCStartUpData::GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);

	if (!NPCStartUpAbilities.IsEmpty())
	{
		for (const TSubclassOf<URPGNPCGameplayAbility>& AbilityClass : NPCStartUpAbilities)
		{
			FGameplayAbilitySpec AbilitySpec;
			if (!BuildAttackAbilitySpec(AbilityClass, ApplyLevel, InASCToGive->GetAvatarActor(), AbilitySpec)) continue;

			InASCToGive->GiveAbility(AbilitySpec);
		}
	}
}

bool UDataAsset_NPCStartUpData::BuildAttackAbilitySpec(TSubclassOf<URPGNPCGameplayAbility> AbilityClass,
	int32 Level, UObject* SourceObject, FGameplayAbilitySpec& OutSpec) const
{
	if (!AbilityClass || Level < 1) return false;
	ERPGAttackHitReactionPolicy Policy = ERPGAttackHitReactionPolicy::Legacy;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass());
		Candidate = Candidate->GetSuperClass())
	{
		if (const auto* Configured = AttackHitReactionPolicies.Find(Candidate))
		{
			Policy = *Configured;
			break; // The nearest authored class wins; one base entry can configure a whole attack family.
		}
	}
	float StunSeconds = 0.f;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass()); Candidate = Candidate->GetSuperClass())
	{
		if (const float* Value = AttackStunDurations.Find(Candidate)) { StunSeconds = *Value; break; }
	}
	FRPGKnockbackSettings Knockback;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass()); Candidate = Candidate->GetSuperClass())
	{
		if (const auto* Value = AttackKnockbacks.Find(Candidate)) { Knockback = *Value; break; }
	}
	FRPGSlowSettings Slow;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass()); Candidate = Candidate->GetSuperClass())
		if (const auto* Value = AttackSlows.Find(Candidate)) { Slow = *Value; break; }
	FRPGDamageTakenIncreaseSettings DamageTakenIncrease;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass()); Candidate = Candidate->GetSuperClass())
		if (const auto* Value = AttackDamageTakenIncreases.Find(Candidate)) { DamageTakenIncrease = *Value; break; }
	FRPGDefenseReductionSettings DefenseReduction;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass()); Candidate = Candidate->GetSuperClass())
		if (const auto* Value = AttackDefenseReductions.Find(Candidate)) { DefenseReduction = *Value; break; }
	FRPGBurnSettings Burn;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass()); Candidate = Candidate->GetSuperClass())
		if (const auto* Value = AttackBurns.Find(Candidate)) { Burn = *Value; break; }
	FRPGStaggerSettings Stagger;
	for (UClass* Candidate=AbilityClass.Get();Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass());Candidate=Candidate->GetSuperClass())
		if(const auto* Value=AttackStaggers.Find(Candidate)){Stagger=*Value;break;}
	FRPGCombatImmunitySettings Immunities;
	for (UClass* Candidate = AbilityClass.Get(); Candidate && Candidate->IsChildOf(URPGNPCGameplayAbility::StaticClass()); Candidate = Candidate->GetSuperClass())
		if (const auto* Value = AbilityImmunities.Find(Candidate)) { Immunities = *Value; break; }
	if (!RPGHitReaction::IsValidPolicy(Policy) || !RPGCombatControl::IsValidStunDuration(StunSeconds) || !Knockback.IsValid() || !Slow.IsValid() || !Burn.IsValid() || !DefenseReduction.IsValid() || !DamageTakenIncrease.IsValid() || !Stagger.IsValid()
		|| !Immunities.IsValid() || (Immunities.IsEnabled() && AbilityClass->GetDefaultObject<URPGGameplayAbility>()->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::NonInstanced)) return false;
	OutSpec = FGameplayAbilitySpec(AbilityClass, Level);
	OutSpec.SourceObject = SourceObject;
	return RPGHitReaction::SetGrantedPolicy(OutSpec, Policy) && RPGCombatControl::SetGrantedStunDuration(OutSpec, StunSeconds)
		&& RPGKnockback::SetGrantedSettings(OutSpec, Knockback) && RPGSlow::SetGrantedSettings(OutSpec, Slow) && RPGBurn::SetGrantedSettings(OutSpec, Burn) && RPGDefenseReduction::SetGrantedSettings(OutSpec, DefenseReduction) && RPGDamageTakenIncrease::SetGrantedSettings(OutSpec, DamageTakenIncrease) && RPGStagger::SetGrantedSettings(OutSpec, Stagger)
		&& RPGCombatImmunity::SetGrantedSettings(OutSpec, Immunities);
}
