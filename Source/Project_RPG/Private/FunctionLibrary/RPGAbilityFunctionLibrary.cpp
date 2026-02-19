// Fill out your copyright notice in the Description page of Project Settings.

#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "ScalableFloat.h"
#include "GameplayEffect.h"

URPGAbilitySystemComponent* URPGAbilityFunctionLibrary::NativeGetWarriorASCFromActor(AActor* InActor)
{
	if (!IsValid(InActor)) return nullptr;

	return Cast<URPGAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InActor));
}

void URPGAbilityFunctionLibrary::AddGameplayTagToActorIfNone(AActor* InActor, FGameplayTag TagToAdd)
{
	URPGAbilitySystemComponent* ASC = NativeGetWarriorASCFromActor(InActor);
	if (!ASC) return;

	if (!ASC->HasMatchingGameplayTag(TagToAdd))
	{
		ASC->AddLooseGameplayTag(TagToAdd);
	}
}

void URPGAbilityFunctionLibrary::RemoveGameplayTagFromActorIfFound(AActor* InActor, FGameplayTag TagToRemove)
{
	URPGAbilitySystemComponent* ASC = NativeGetWarriorASCFromActor(InActor);
	if (!ASC) return;

	if (ASC->HasMatchingGameplayTag(TagToRemove))
	{
		ASC->RemoveLooseGameplayTag(TagToRemove);
	}
}

bool URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck)
{
	URPGAbilitySystemComponent* ASC = NativeGetWarriorASCFromActor(InActor);
	return ASC ? ASC->HasMatchingGameplayTag(TagToCheck) : false;
}

void URPGAbilityFunctionLibrary::BP_DoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck, ERPGConfirmType& OutConfirmType)
{
	OutConfirmType = NativeDoesActorHaveTag(InActor, TagToCheck) ? ERPGConfirmType::Yes : ERPGConfirmType::No;
}

float URPGAbilityFunctionLibrary::GetScalableFloatValueAtLevel(const FScalableFloat& InScalableFloat, float InLevel)
{
	return InScalableFloat.GetValueAtLevel(InLevel);
}

bool URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToTargetActor(AActor* InInstigator, AActor* InTargetActor, const FGameplayEffectSpecHandle& InSpecHandle)
{
	if (!InSpecHandle.Data.IsValid()) return false;

	URPGAbilitySystemComponent* SourceASC = NativeGetWarriorASCFromActor(InInstigator);
	URPGAbilitySystemComponent* TargetASC = NativeGetWarriorASCFromActor(InTargetActor);

	if (!SourceASC || !TargetASC) return false;

	FActiveGameplayEffectHandle ActiveGameplayEffectHandle = 
		SourceASC->ApplyGameplayEffectSpecToTarget(*InSpecHandle.Data, TargetASC);

	return ActiveGameplayEffectHandle.WasSuccessfullyApplied();
}
