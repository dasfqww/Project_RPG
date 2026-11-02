// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/RPGCharacterAnimInstance.h"
#include "Character/RPGBaseCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

void URPGCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	ResetLocomotion();
	RefreshOwnerReferences();
}

void URPGCharacterAnimInstance::RefreshOwnerReferences()
{
	APawn* Pawn = TryGetPawnOwner();
	OwningCharacter = IsValid(Pawn) ? Cast<ARPGBaseCharacter>(Pawn) : nullptr;
	OwningMovementComponent = IsValid(OwningCharacter) ? OwningCharacter->GetCharacterMovement() : nullptr;
}

void URPGCharacterAnimInstance::ResetLocomotion()
{
	LocomotionFrame = {};
	GroundSpeed = 0.f;
	bHasAcceleration = false;
	LocomotionDirection = 0.f;
}

void URPGCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	RefreshOwnerReferences();
	LocomotionFrame = {};
	if (IsValid(OwningCharacter) && IsValid(OwningMovementComponent))
	{
		LocomotionFrame.Velocity = OwningCharacter->GetVelocity();
		LocomotionFrame.Acceleration = OwningMovementComponent->GetCurrentAcceleration();
		LocomotionFrame.Rotation = OwningCharacter->GetActorRotation();
		LocomotionFrame.bValid = true;
	}
}

void URPGCharacterAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	GroundSpeed = LocomotionFrame.Velocity.Size2D();
	bHasAcceleration = LocomotionFrame.Acceleration.SizeSquared2D() > 0.f;
	LocomotionDirection = UKismetAnimationLibrary::CalculateDirection(
		LocomotionFrame.Velocity, LocomotionFrame.Rotation);
}

void URPGCharacterAnimInstance::NativeUninitializeAnimation()
{
	OwningCharacter = nullptr;
	OwningMovementComponent = nullptr;
	ResetLocomotion();
	Super::NativeUninitializeAnimation();
}
