// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGGameplayAbility.h"
#include "RPGNPCGameplayAbility.generated.h"

class ARPGNonPlayerCharacter;
class UNPCCombatComponent;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGNPCGameplayAbility : public URPGGameplayAbility
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		ARPGNonPlayerCharacter* GetNonPlayerCharacterFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		UNPCCombatComponent* GetNPCombatComponentFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		FGameplayEffectSpecHandle MakeEnemyDamageEffectSpecHandle(TSubclassOf<UGameplayEffect> EffectClass, 
			const FScalableFloat& InDamageScalableFloat);

private:
	TWeakObjectPtr<ARPGNonPlayerCharacter> CachedRPGNonPlayerCharacter;
};
