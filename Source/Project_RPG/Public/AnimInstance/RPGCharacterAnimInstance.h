// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstance/RPGBaseAnimInstance.h"
#include "RPGCharacterAnimInstance.generated.h"

class ARPGBaseCharacter;
class UCharacterMovementComponent;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGCharacterAnimInstance : public URPGBaseAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds);
protected:
	UPROPERTY()
		TObjectPtr<ARPGBaseCharacter> OwningCharacter;

	UPROPERTY()
		TObjectPtr<UCharacterMovementComponent> OwningMovementComponent;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		float GroundSpeed;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		bool bHasAcceleration;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		float LocomotionDirection;
};
