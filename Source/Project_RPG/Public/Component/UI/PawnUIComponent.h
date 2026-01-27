// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "PawnUIComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPercentChangedDelegate, float, NewPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthTextChangedDelegate, FString, HealthText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnManaTextChanged, FString, ManaText);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UPawnUIComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable)
	FOnPercentChangedDelegate OnCurrentHealthChanged;

	UPROPERTY(BlueprintAssignable)
	FOnHealthTextChangedDelegate OnHealthTextChangedDelegate;

	UPROPERTY(BlueprintAssignable)
	FOnPercentChangedDelegate OnCurrentManaChanged;

	UPROPERTY(BlueprintAssignable)
	FOnManaTextChanged OnManaTextChanged;
};
