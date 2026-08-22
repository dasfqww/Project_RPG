// Fill out your copyright notice in the Description page of Project Settings.

#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "ScalableFloat.h"
#include "GameplayEffect.h"
#include "RPGGameplayTags.h"

namespace RPGAbilityFunctionLibrary
{
	bool TryGetDamageMagnitude(
		const FGameplayEffectSpec& Spec,
		float& OutDamage)
	{
		const FGameplayTag D1DamageTag = FGameplayTag::RequestGameplayTag(
			TEXT("SetByCaller.BaseDamage"), false);
		const FGameplayTag DamageTags[] = {
			RPGGameplayTags::Shared_SetByCaller_BaseDamage,
			D1DamageTag
		};

		for (const FGameplayTag& DamageTag : DamageTags)
		{
			if (!DamageTag.IsValid())
			{
				continue;
			}
			if (const float* Magnitude =
				Spec.SetByCallerTagMagnitudes.Find(DamageTag))
			{
				OutDamage = *Magnitude;
				return true;
			}
		}

		OutDamage = 0.0f;
		return false;
	}
}

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
	if (!IsValid(InInstigator) || !InInstigator->HasAuthority() ||
		!IsValid(InTargetActor) || !InSpecHandle.Data.IsValid())
	{
		return false;
	}

	const FVector ImpactPoint = InTargetActor->GetActorLocation();
	const FHitResult ServerHit(
		InTargetActor,
		nullptr,
		ImpactPoint,
		(ImpactPoint - InInstigator->GetActorLocation()).GetSafeNormal());
	const FRPGSkillSecurityProfile CompatibilityProfile;
	return ApplyGameplayEffectSpecHandleToServerHit(
		InInstigator,
		ServerHit,
		InSpecHandle,
		CompatibilityProfile);
}

bool URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
	AActor* InInstigator,
	const FHitResult& ServerHit,
	const FGameplayEffectSpecHandle& InSpecHandle,
	const FRPGSkillSecurityProfile& SecurityProfile,
	FActiveGameplayEffectHandle* OutActiveHandle)
{
	if (OutActiveHandle)
	{
		*OutActiveHandle = FActiveGameplayEffectHandle();
	}
	AActor* TargetActor = ServerHit.GetActor();
	if (!IsValid(InInstigator) || !InInstigator->HasAuthority() ||
		!IsValid(TargetActor) || TargetActor == InInstigator ||
		!InSpecHandle.Data.IsValid())
	{
		return false;
	}

	float Damage = 0.0f;
	if (RPGAbilityFunctionLibrary::TryGetDamageMagnitude(
		*InSpecHandle.Data,
		Damage))
	{
		FText RejectionReason;
		if (!URPGSecurityBlueprintLibrary::ValidateAuthorizedServerHit(
			InInstigator,
			ServerHit,
			Damage,
			SecurityProfile,
			RejectionReason))
		{
			return false;
		}
	}

	URPGAbilitySystemComponent* SourceASC =
		NativeGetWarriorASCFromActor(InInstigator);
	URPGAbilitySystemComponent* TargetASC =
		NativeGetWarriorASCFromActor(TargetActor);
	if (!SourceASC || !TargetASC || SourceASC == TargetASC)
	{
		return false;
	}

	const FActiveGameplayEffectHandle ActiveGameplayEffectHandle =
		SourceASC->ApplyGameplayEffectSpecToTarget(
			*InSpecHandle.Data,
			TargetASC);
	if (OutActiveHandle)
	{
		*OutActiveHandle = ActiveGameplayEffectHandle;
	}
	return ActiveGameplayEffectHandle.WasSuccessfullyApplied();
}
