// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/RPGBaseAnimInstance.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"

void URPGBaseAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CapturedOwnerTags.Reset();
}

void URPGBaseAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	CapturedOwnerTags.Reset();
	if (APawn* Pawn = TryGetPawnOwner(); IsValid(Pawn))
	{
		if (const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn))
		{
			CapturedOwnerTags = ASC->GetOwnedGameplayTags();
		}
	}
}

void URPGBaseAnimInstance::NativeUninitializeAnimation()
{
	CapturedOwnerTags.Reset();
	Super::NativeUninitializeAnimation();
}

bool URPGBaseAnimInstance::DoesOwnerHaveTag(FGameplayTag TagToCheck) const
{
	return CapturedOwnerTags.HasTag(TagToCheck);
}
