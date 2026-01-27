// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DataAsset_StartUpDataBase.generated.h"

class URPGGameplayAbility;
class URPGAbilitySystemComponent;
class UGameplayEffect;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDataAsset_StartUpDataBase : public UDataAsset
{
	GENERATED_BODY()
public:
	virtual void GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "StartUpData")
		TArray< TSubclassOf < URPGGameplayAbility > > ActivateOnGivenAbilities;

	UPROPERTY(EditDefaultsOnly, Category = "StartUpData")
		TArray< TSubclassOf < URPGGameplayAbility > > ReactiveAbilities;
	
	UPROPERTY(EditDefaultsOnly, Category = "StartUpData")
		TArray< TSubclassOf < UGameplayEffect > > StartUpGameplayEffects;

	void GrantAbilities(const TArray< TSubclassOf < URPGGameplayAbility > >& InAbilitiesToGive,
			URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1);


};