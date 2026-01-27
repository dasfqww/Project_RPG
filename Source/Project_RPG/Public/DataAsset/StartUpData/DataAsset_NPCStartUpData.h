// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "DataAsset_NPCStartUpData.generated.h"

class URPGNPCGameplayAbility;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDataAsset_NPCStartUpData : public UDataAsset_StartUpDataBase
{
	GENERATED_BODY()
public:
	virtual void GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "StartUpData", meta = (TitleProperty = "InputTag"))
		TArray<TSubclassOf<URPGNPCGameplayAbility>> NPCStartUpAbilities;
};
