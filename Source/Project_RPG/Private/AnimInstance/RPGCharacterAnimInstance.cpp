// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/RPGCharacterAnimInstance.h"
#include "Character/RPGBaseCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

#include"RPGDebugHelper.h"

void URPGCharacterAnimInstance::NativeInitializeAnimation()
{
	OwningCharacter = Cast<ARPGBaseCharacter>(TryGetPawnOwner());

	if (OwningCharacter)
	{
		OwningMovementComponent = OwningCharacter->GetCharacterMovement();
	}
}

void URPGCharacterAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	if (!OwningCharacter||!OwningMovementComponent)
	{
		return;
	}

	GroundSpeed = OwningCharacter->GetVelocity().Size2D();

	bHasAcceleration = OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D() > 0.f;
	
	LocomotionDirection = UKismetAnimationLibrary::CalculateDirection(OwningCharacter->GetVelocity(), 
		OwningCharacter->GetActorRotation());
}
