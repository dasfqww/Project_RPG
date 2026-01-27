// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/RPGComboSkillAbility.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/RPGBaseCharacter.h"
#include "Attribute/RPGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include <DataTable/SkillData.h>
#include "RPGFunctionLibrary.h"
#include "RPGGameplayTags.h"

#include "RPGDebugHelper.h"

URPGComboSkillAbility::URPGComboSkillAbility()
{
	CurrentComboCount = InitComboCount;
}

void URPGComboSkillAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, 
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ComboCountResetTimerHandle);
		ComboCountResetTimerHandle.Invalidate();
	}

	PlaySkillMontage();
	ExecWaitGameplayEvent();

}

void URPGComboSkillAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	GetWorld()->GetTimerManager().SetTimer(
		ComboCountResetTimerHandle,
		[this](){ResetComboCount(); },
		TimeToTimerReset,
		false
	);

}

void URPGComboSkillAbility::PlaySkillMontage()
{
	Super::PlaySkillMontage();

	//Debug::Print(TEXT("Current Combo Count"), CurrentComboCount);

	const FMultipleSectionData* SelectedAttackData = &DefaultAttackSectionData;

	//float AttackSpeed=CachedAttributeSet->GetAttackSpeed();

	//Debug::Print("Attack Speed: ", AttackSpeed);

	if (URPGFunctionLibrary::NativeDoesActorHaveTag
	(GetOwningActorFromActorInfo(), RPGGameplayTags::Player_Status_Rage_Active))
	{
		SelectedAttackData = &DefaultAttackSectionData_Rage;
	}

	FName StartSection = *SelectedAttackData->SectionNamesToPlay.Find(CurrentComboCount);

	UAbilityTask_PlayMontageAndWait* PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			SelectedAttackData->MontageToPlay,
			AttackSpeed,
			StartSection
		);

	PlayAttackTask->OnCompleted.AddDynamic(this, &URPGPlayerGameplayAbility::OnCompleteCallBack);
	PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	PlayAttackTask->ReadyForActivation();

	if (CurrentComboCount==DefaultAttackSectionData.SectionNamesToPlay.Num())
	{
		//Debug::Print(TEXT("Current Combo Count reset.."));
		CurrentComboCount = 1;
		//Debug::Print(TEXT("Current Combo Count : "), CurrentComboCount);
	}

	else
	{
		//Debug::Print(TEXT("Current Combo Count increased.."));
		CurrentComboCount++;
		//Debug::Print(TEXT("Current Combo Count : "), CurrentComboCount);
	}
}

//void URPGComboSkillAbility::HandleApplyDamage(const FGameplayEventData& InGameplayEventData)
//{
//	Super::HandleApplyDamage(InGameplayEventData);
//
//	AActor* CachedTargetActor = const_cast<AActor*>(InGameplayEventData.Target.Get());
//
//	float WeaponBaseDamage =
//		GetPlayerCombatComponentFromActorInfo()->GetPlayerCurrentEquipWeaponDamageAtLevel(GetAbilityLevel());
//
//	float CalcDamage = CalculateCriticalDamage(CachedTargetActor, WeaponBaseDamage);
//
//	FGameplayEffectSpecHandle GameplayEffectSpecHandle;
//	GameplayEffectSpecHandle =
//		MakePlayerDamageEffectSpecHandle(DamageEffectClass, CalcDamage, CurrentAttackTypeTag, UsedComboCount);
//
//	ERPGSuccessType SuccessType;
//
//	BP_ApplyEffectSpecHandleToTarget(CachedTargetActor, GameplayEffectSpecHandle, SuccessType);
//
//	// SuccessType 값에 따라 분기 처리
//	if (SuccessType == ERPGSuccessType::Successful)
//	{
//		UE_LOG(LogTemp, Log, TEXT("Effect applied successfully."));
//
//		FGameplayEffectContextHandle EmptyContext;
//		K2_ExecuteGameplayCue(WeaponHitSoundCueTag, EmptyContext);
//
//		FGameplayEventData EventData;
//
//		// Instigator를 어빌리티 소유자로 설정
//		if (CurrentActorInfo)
//		{
//			EventData.Instigator = CurrentActorInfo->OwnerActor.Get();
//		}
//
//		EventData.Target = CachedTargetActor;
//
//		// 이벤트 전송(히트 리액션 태그 전송)
//		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(CachedTargetActor, HitReactTag, EventData);
//		
//		GainIdentity();
//	}
//	else
//	{
//		UE_LOG(LogTemp, Warning, TEXT("Effect application failed."));
//	}
//}

void URPGComboSkillAbility::ResetComboCount()
{
	CurrentComboCount = 1;
	//Debug::Print(TEXT("Current Combo Count Reset.."));
}
