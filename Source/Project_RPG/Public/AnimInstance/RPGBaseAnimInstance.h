// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GameplayTagContainer.h"
#include "RPGBaseAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGBaseAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
protected:
	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe))
		bool DoesOwnerHaveTag(FGameplayTag TagToCheck) const;
};
