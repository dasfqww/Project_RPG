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
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeUninitializeAnimation() override;
protected:
	// Captured on the game thread; evaluation only consumes these value types.
	struct FLocomotionFrame
	{
		FVector Velocity = FVector::ZeroVector;
		FVector Acceleration = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		bool bValid = false;
	};
	FLocomotionFrame LocomotionFrame;

	UPROPERTY()
		TObjectPtr<ARPGBaseCharacter> OwningCharacter;

	UPROPERTY()
		TObjectPtr<UCharacterMovementComponent> OwningMovementComponent;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		float GroundSpeed = 0.f;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		bool bHasAcceleration = false;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		float LocomotionDirection = 0.f;

private:
	friend class FRPGAnimInstanceStateTest;
	void RefreshOwnerReferences();
	void ResetLocomotion();
};
