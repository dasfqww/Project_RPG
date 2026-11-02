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
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeUninitializeAnimation() override;

protected:
	// Read the tags captured for this animation update, never the live ASC on a worker.
	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe))
		bool DoesOwnerHaveTag(FGameplayTag TagToCheck) const;

private:
	friend class FRPGAnimInstanceStateTest;
	FGameplayTagContainer CapturedOwnerTags;
};
