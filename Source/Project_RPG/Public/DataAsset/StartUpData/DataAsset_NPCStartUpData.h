// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Combat/RPGHitReactionTypes.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGBurnTypes.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "Combat/RPGDamageTakenIncreaseTypes.h"
#include "Combat/RPGStaggerTypes.h"
#include "Combat/RPGCombatImmunityTypes.h"
#include "DataAsset_NPCStartUpData.generated.h"

class URPGNPCGameplayAbility;
struct FGameplayAbilitySpec;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDataAsset_NPCStartUpData : public UDataAsset_StartUpDataBase
{
	GENERATED_BODY()
public:
	virtual void GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1) override;
	bool BuildAttackAbilitySpec(TSubclassOf<URPGNPCGameplayAbility> AbilityClass, int32 Level,
		UObject* SourceObject, FGameplayAbilitySpec& OutSpec) const;

	/** Omitted attacks retain legacy behavior; the same GA can use different policies on different loadouts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StartUpData|Combat")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, ERPGAttackHitReactionPolicy> AttackHitReactionPolicies;
	/** Nearest configured attack ancestor wins; omitted attacks do not apply a stun. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StartUpData|Combat")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, float> AttackStunDurations;
	/** Nearest parent entry wins. Distance and height both zero disable one child attack. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StartUpData|Combat")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, FRPGKnockbackSettings> AttackKnockbacks;
	/** Nearest parent entry wins; zero percent disables an individual attack. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StartUpData|Combat")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, FRPGSlowSettings> AttackSlows;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Status")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, FRPGBurnSettings> AttackBurns;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StartUpData|Combat")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, FRPGDefenseReductionSettings> AttackDefenseReductions;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StartUpData|Combat")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, FRPGDamageTakenIncreaseSettings> AttackDamageTakenIncreases;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Control")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, FRPGStaggerSettings> AttackStaggers;
	/** Caster protection; nearest parent wins, zero explicitly disables inheritance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StartUpData|Combat")
	TMap<TSubclassOf<URPGNPCGameplayAbility>, FRPGCombatImmunitySettings> AbilityImmunities;

private:
	UPROPERTY(EditDefaultsOnly, Category = "StartUpData", meta = (TitleProperty = "InputTag"))
		TArray<TSubclassOf<URPGNPCGameplayAbility>> NPCStartUpAbilities;
};
