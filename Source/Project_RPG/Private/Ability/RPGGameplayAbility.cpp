// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/RPGGameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "RPGGameplayTags.h"
#include "Character/RPGBaseCharacter.h"
#include "Controller/RPGPlayerController.h"
#include "Component/RPGSecurityValidationComponent.h"
#include"Attribute/RPGAttributeSet.h"

bool URPGGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!AvatarActor || !AvatarActor->HasAuthority())
	{
		return true;
	}
	if (!ShouldApplyServerActivationRateLimit())
	{
		return true;
	}

	URPGSecurityValidationComponent* Security =
		AvatarActor->FindComponentByClass<URPGSecurityValidationComponent>();
	FString RejectionReason;
	return !Security || Security->CanAcceptAbilityActivation(
		GetClass(),
		RejectionReason);
}

void URPGGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (AvatarActor && AvatarActor->HasAuthority() &&
		ShouldApplyServerActivationRateLimit())
	{
		if (URPGSecurityValidationComponent* Security =
			AvatarActor->FindComponentByClass<URPGSecurityValidationComponent>())
		{
			Security->RecordAbilityActivation(GetClass());
		}
	}
}

bool URPGGameplayAbility::ShouldApplyServerActivationRateLimit() const
{
	return bCountTowardServerActivationRateLimit &&
		AbilityActivationPolicy == ERPGAbilityActivationPolicy::OnTriggered &&
		ActivationPolicy != ERPGGladiatorAbilityActivationPolicy::OnSpawn &&
		NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void URPGGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	if (AbilityActivationPolicy == ERPGAbilityActivationPolicy::OnGiven)
	{
		if (ActorInfo && !Spec.IsActive())
		{
			ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		}
	}
}

void URPGGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	if (AbilityActivationPolicy == ERPGAbilityActivationPolicy::OnGiven)
	{
		if (ActorInfo)
		{
			ActorInfo->AbilitySystemComponent->ClearAbility(Handle);
		}
	}
}

void URPGGameplayAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);

	if (ActorInfo&&ActorInfo->AbilitySystemComponent.IsValid())
	{
		// ���� �� ���� ĳ��
		CachedAttributeSet = ActorInfo->AbilitySystemComponent->GetSet<URPGAttributeSet>();
	}

	if (ActivationPolicy == ERPGGladiatorAbilityActivationPolicy::OnSpawn && ActorInfo && !Spec.IsActive())
	{
		ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle);
	}
}

UPawnCombatComponent* URPGGameplayAbility::GetPawnCombatComponentFromActorInfo() const
{
	return GetAvatarActorFromActorInfo()->FindComponentByClass<UPawnCombatComponent>();
}

URPGAbilitySystemComponent* URPGGameplayAbility::GetRPGAbilitySystemComponentFromActorInfo() const
{
	return Cast<URPGAbilitySystemComponent>(CurrentActorInfo->AbilitySystemComponent);
}

ARPGPlayerController* URPGGameplayAbility::GetLyraPlayerControllerFromActorInfo() const
{
	return CurrentActorInfo ? Cast<ARPGPlayerController>(CurrentActorInfo->PlayerController.Get()) : nullptr;
}

FActiveGameplayEffectHandle URPGGameplayAbility::NativeApplyEffectSpecHandleToTarget(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle)
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor || !AvatarActor->HasAuthority() ||
		!IsValid(TargetActor) || !InSpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC)
	{
		return FActiveGameplayEffectHandle();
	}
	URPGAbilitySystemComponent* SourceASC =
		GetRPGAbilitySystemComponentFromActorInfo();
	return SourceASC
		? SourceASC->ApplyGameplayEffectSpecToTarget(
			*InSpecHandle.Data,
			TargetASC)
		: FActiveGameplayEffectHandle();
}

FActiveGameplayEffectHandle URPGGameplayAbility::BP_ApplyEffectSpecHandleToTarget
	(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle, ERPGSuccessType& OutSuccessType)
{
	AActor* SourceActor = GetAvatarActorFromActorInfo();
	FActiveGameplayEffectHandle ActiveGameplayEffectHandle;
	bool bApplied = false;
	if (IsValid(SourceActor) && IsValid(TargetActor))
	{
		const FVector ImpactPoint = TargetActor->GetActorLocation();
		const FHitResult ServerHit(
			TargetActor,
			nullptr,
			ImpactPoint,
			(ImpactPoint - SourceActor->GetActorLocation()).GetSafeNormal());
		const FRPGSkillSecurityProfile CompatibilityProfile;
		bApplied =
			URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
				SourceActor,
				ServerHit,
				InSpecHandle,
				CompatibilityProfile,
				&ActiveGameplayEffectHandle);
	}
	OutSuccessType = bApplied
		? ERPGSuccessType::Successful
		: ERPGSuccessType::Failed;
	return ActiveGameplayEffectHandle;
}

void URPGGameplayAbility::ApplyGameplayEffectSpecHandleToHitResults(const FGameplayEffectSpecHandle& InSpecHandle, const TArray<FHitResult>& InHitResults)
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor || !AvatarActor->HasAuthority() ||
		!InSpecHandle.IsValid() || InHitResults.IsEmpty())
	{
		return;
	}

	APawn* OwningPawn = CastChecked<APawn>(GetAvatarActorFromActorInfo());

	for (const FHitResult& Hit : InHitResults)
	{
		if (APawn* HitPawn = Cast<APawn>(Hit.GetActor()))
		{
			if (URPGCombatFunctionLibrary::IsTargetPawnHostile(OwningPawn, HitPawn))
			{
				const FRPGSkillSecurityProfile CompatibilityProfile;
				if (URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
					OwningPawn,
					Hit,
					InSpecHandle,
					CompatibilityProfile))
				{
					FGameplayEventData Data;
					Data.Instigator = OwningPawn;
					Data.Target = HitPawn;

					UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
						HitPawn,
						RPGGameplayTags::Shared_Ability_HitReact,
						Data
					);
				}
			}
		}
	}
}

////������ ��Ʈ UIŬ������ ��� �����غ���.
//void URPGGameplayAbility::DisplayDamageEffect
//	(AActor* InCachedTargetActor, float InWeaponBaseDamage, bool bCritical)
//{
//	//���߿� �����丵 �� ��
//	if (InCachedTargetActor)
//	{
//		ARPGBaseCharacter* TargetCharacter = Cast<ARPGBaseCharacter>(InCachedTargetActor);
//
//		if (TargetCharacter)
//		{
//			// ������ ��Ʈ�� ǥ���ϴ� �Լ� ȣ��
//			TargetCharacter->
//				ShowDamageFont(InWeaponBaseDamage, InCachedTargetActor->GetActorLocation(), bCritical);
//		}
//	}
//}
//
void URPGGameplayAbility::DisplayInvincibleEffect(AActor* InCachedTargetActor)
{
	if (InCachedTargetActor)
	{
		ARPGBaseCharacter* TargetCharacter = Cast<ARPGBaseCharacter>(InCachedTargetActor);

		if (TargetCharacter)
		{
			TargetCharacter->ShowInvincibleFont(InCachedTargetActor->GetActorLocation());
		}
	}
}
