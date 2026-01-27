// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/RPGGameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "RPGFunctionLibrary.h"
#include "RPGGameplayTags.h"
#include "Character/RPGBaseCharacter.h"
#include"Attribute/RPGAttributeSet.h"

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
		// 최초 한 번만 캐싱
		CachedAttributeSet = ActorInfo->AbilitySystemComponent->GetSet<URPGAttributeSet>();
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

FActiveGameplayEffectHandle URPGGameplayAbility::NativeApplyEffectSpecHandleToTarget(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle)
{
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);

	check(TargetASC&&InSpecHandle.IsValid());

	return GetRPGAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*InSpecHandle.Data, TargetASC);
}

FActiveGameplayEffectHandle URPGGameplayAbility::BP_ApplyEffectSpecHandleToTarget
	(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle, ERPGSuccessType& OutSuccessType)
{
	FActiveGameplayEffectHandle ActiveGameplayEffectHandle = NativeApplyEffectSpecHandleToTarget(TargetActor, InSpecHandle);

	OutSuccessType = ActiveGameplayEffectHandle.WasSuccessfullyApplied() ? 
		ERPGSuccessType::Successful : ERPGSuccessType::Failed;

	return ActiveGameplayEffectHandle;
}

void URPGGameplayAbility::ApplyGameplayEffectSpecHandleToHitResults(const FGameplayEffectSpecHandle& InSpecHandle, const TArray<FHitResult>& InHitResults)
{
	if (InHitResults.IsEmpty())
	{
		return;
	}

	APawn* OwningPawn = CastChecked<APawn>(GetAvatarActorFromActorInfo());

	for (const FHitResult& Hit : InHitResults)
	{
		if (APawn* HitPawn = Cast<APawn>(Hit.GetActor()))
		{
			if (URPGFunctionLibrary::IsTargetPawnHostile(OwningPawn, HitPawn))
			{
				FActiveGameplayEffectHandle ActiveGameplayEffectHandle = NativeApplyEffectSpecHandleToTarget(HitPawn, InSpecHandle);

				if (ActiveGameplayEffectHandle.WasSuccessfullyApplied())
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

////데미지 폰트 UI클래스로 기능 이전해볼것.
//void URPGGameplayAbility::DisplayDamageEffect
//	(AActor* InCachedTargetActor, float InWeaponBaseDamage, bool bCritical)
//{
//	//나중에 리팩토링 할 것
//	if (InCachedTargetActor)
//	{
//		ARPGBaseCharacter* TargetCharacter = Cast<ARPGBaseCharacter>(InCachedTargetActor);
//
//		if (TargetCharacter)
//		{
//			// 데미지 폰트를 표시하는 함수 호출
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
