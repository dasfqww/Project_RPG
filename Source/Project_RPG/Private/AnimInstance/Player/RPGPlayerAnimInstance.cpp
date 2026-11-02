// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimInstance/Player/RPGPlayerAnimInstance.h"
#include "Character/RPGPlayer.h"

void URPGPlayerAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwningPlayerCharacter = Cast<ARPGPlayer>(OwningCharacter);
	ResetRelaxState();
}

void URPGPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	OwningPlayerCharacter = Cast<ARPGPlayer>(OwningCharacter);
}

void URPGPlayerAnimInstance::ResetRelaxState()
{
	IdleElapsedTime = 0.f;
	bShouldEnterRelaxState = false;
}

void URPGPlayerAnimInstance::NativeUninitializeAnimation()
{
	OwningPlayerCharacter = nullptr;
	ResetRelaxState();
	Super::NativeUninitializeAnimation();
}

void URPGPlayerAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	if (!LocomotionFrame.bValid || bHasAcceleration)
	{
		ResetRelaxState();
	}

	else
	{
		IdleElapsedTime += DeltaSeconds;
		bShouldEnterRelaxState = (IdleElapsedTime >= EnterRelaxStateThreshold);
	}
}
