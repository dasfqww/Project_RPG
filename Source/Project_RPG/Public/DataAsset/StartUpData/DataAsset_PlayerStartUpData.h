// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Type/RPGStructTypes.h"
#include "DataAsset_PlayerStartUpData.generated.h"



/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDataAsset_PlayerStartUpData : public UDataAsset_StartUpDataBase
{
	GENERATED_BODY()
public:
	virtual void GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FRPGPlayerIdentityData IdentityData;

private:
	UPROPERTY(EditDefaultsOnly, Category = "StartUpData", meta=(TitleProperty="InputTag"))
		TArray<FRPGPlayerAbilitySet> PlayerStartUpAbilitySets;
};
