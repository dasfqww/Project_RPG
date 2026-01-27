// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/RPGBaseAnimInstance.h"
#include "RPGFunctionLibrary.h"

bool URPGBaseAnimInstance::DoesOwnerHaveTag(FGameplayTag TagToCheck) const
{
	if (APawn* OwningPawn = TryGetPawnOwner())
	{
		return URPGFunctionLibrary::NativeDoesActorHaveTag(OwningPawn, TagToCheck);
	}
	return false;
}
