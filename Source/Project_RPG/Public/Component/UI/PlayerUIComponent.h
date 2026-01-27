// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/UI/PawnUIComponent.h"
#include "GameplayTagContainer.h"
#include "PlayerUIComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAbilityCooldownBeginDelegate, FGameplayTag, AbilityInputTag, float, TotalCooldownTime, float, RemainingCooldownTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProgressBarShow);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProgressBarHidden);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChargeCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActivateIdentity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeactivateIdentity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProgressBarTextChanged, FName, SkillName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProgressTimeChanged, FString, TimeText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNoticeTextChanged, FText, Text);


/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UPlayerUIComponent : public UPawnUIComponent
{
	GENERATED_BODY()
public:
	

	UPROPERTY(BlueprintAssignable)
		FOnPercentChangedDelegate OnCurrentRageChanged;

	UPROPERTY(BlueprintAssignable)
		FOnPercentChangedDelegate OnCurrentIdentityGaugeChanged;
		
	UPROPERTY(BlueprintAssignable)
		FOnPercentChangedDelegate OnCurrentProgressChanged;
		
	UPROPERTY(BlueprintAssignable)
	FOnProgressBarShow OnProgressBarShow;
	
	UPROPERTY(BlueprintAssignable)
	FOnProgressBarHidden OnProgressBarHidden;
	
	UPROPERTY(BlueprintAssignable)
	FOnChargeCompleted OnChargeCompleted;
	
	UPROPERTY(BlueprintAssignable)
	FOnProgressBarTextChanged OnProgressBarTextChanged;

	UPROPERTY(BlueprintAssignable)
	FOnProgressTimeChanged OnProgressTimeChanged;

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnNoticeTextChanged OnNoticeTextChanged;

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnActivateIdentity OnActivateIdentity;
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnDeactivateIdentity OnDeactivateIdentity;

	UPROPERTY(BlueprintCallable, BlueprintAssignable)
	FOnAbilityCooldownBeginDelegate OnAbilityCooldownBegin;
};
