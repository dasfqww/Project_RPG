// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Skill/RPGInstantSkillAbility.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Attribute/RPGAttributeSet.h"


bool URPGInstantSkillAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);

}

void URPGInstantSkillAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	PlaySkillMontage();
	ExecWaitGameplayEvent();
	CoolDown();
}

void URPGInstantSkillAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);


}

void URPGInstantSkillAbility::PlaySkillMontage()
{
	Super::PlaySkillMontage();

	//AttackSpeed = CachedAttributeSet->GetAttackSpeed();

	UAbilityTask_PlayMontageAndWait* PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			SingleSectionData.MontageToPlay,
			AttackSpeed,
			SingleSectionData.SectionNameToPlay
		);
	
	PlayAttackTask->OnCompleted.AddDynamic(this, &URPGPlayerGameplayAbility::OnCompleteCallBack);
	PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	PlayAttackTask->ReadyForActivation();
}

//void URPGInstantSkillAbility::HandleApplyDamage(const FGameplayEventData& InGameplayEventData)
//{
//	Super::HandleApplyDamage(InGameplayEventData);
//
//	AActor* CachedTargetActor = const_cast<AActor*>(InGameplayEventData.Target.Get());
//
//	float WeaponBaseDamage=
//		GetPlayerCombatComponentFromActorInfo()->GetPlayerCurrentEquipWeaponDamageAtLevel(GetAbilityLevel());
//
//	float CalcDamage = CalculateCriticalDamage(CachedTargetActor, WeaponBaseDamage);
//
//	FGameplayEffectSpecHandle GameplayEffectSpecHandle;
//	GameplayEffectSpecHandle =
//		MakePlayerDamageEffectSpecHandle(DamageEffectClass, CalcDamage, CurrentAttackTypeTag, 4);
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
//
//	else
//	{
//		UE_LOG(LogTemp, Warning, TEXT("Effect application failed."));
//	}
//}
