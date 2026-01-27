// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Skill/RPGToggleSkillAbility.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"

#include "RPGDebugHelper.h"

bool URPGToggleSkillAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, 
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void URPGToggleSkillAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	//StartSkill();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	
	//bIsActive = !bIsActive;

	//Debug::Print("bActive is ", bIsActive);

	/*if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		if (ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Player.Status.Holding"))))
		{
			CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
			Debug::Print("cancel ability");
			return;
		}
	}*/

	ToggleSkill();
	ExecWaitGameplayEvent();
	CoolDown();
}

void URPGToggleSkillAbility::CancelAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateCancel)
{
	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancel);

	//bIsActive = false;

	Debug::Print("Skill Cancelled...");
}

void URPGToggleSkillAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	GetWorld()->GetTimerManager().ClearTimer(ToggleTimerHandle);
	HiddenProgressBar(ActorInfo);
	CurrentToggleTime = 0.f;

	//PlayAttackTask = nullptr;

	/*URPGAbilitySystemComponent* ASC = Cast<URPGAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	if (ASC)
	{
		ASC->CurrentMontageStop(0.f);
	}*/

	FName Section = MultipleSectionData.SectionNamesToPlay[1];

	UAbilityTask_PlayMontageAndWait* PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			MultipleSectionData.MontageToPlay,
			AttackSpeed,
			Section
		);

	PlayAttackTask->OnCompleted.AddDynamic(this, &URPGPlayerGameplayAbility::OnCompleteCallBack);
	PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	PlayAttackTask->ReadyForActivation();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGToggleSkillAbility::PlaySkillMontage()
{
	Super::PlaySkillMontage();

	//if (PlayAttackTask)
	//{
	//	if (PlayAttackTask->IsActive())
	//	{
	//		// 이미 실행 중임
	//		Debug::Print("Task is working..");
	//		return;
	//	}
	//	
	//}

	//float AttackSpeed = CachedAttributeSet->GetAttackSpeed();

	FName Section = MultipleSectionData.SectionNamesToPlay[0];

	UAbilityTask_PlayMontageAndWait* PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			MultipleSectionData.MontageToPlay,
			AttackSpeed,
			Section
		);

	PlayAttackTask->OnCompleted.AddDynamic(this, &URPGPlayerGameplayAbility::OnCompleteCallBack);
	PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	//PlayAttackTask->OnCancelled.AddDynamic(this, &ThisClass::EndOrCancelSkill);
	PlayAttackTask->ReadyForActivation();
}

void URPGToggleSkillAbility::ToggleSkill()
{
	if (bIsActive)
	{
		Debug::Print("activeskiil");
		StartSkill();		
	}

	else 
	{
		Debug::Print("cancelskiil");
		//EndOrCancelSkill();
		GetWorld()->GetTimerManager().ClearTimer(ToggleTimerHandle);
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}

void URPGToggleSkillAbility::UpdateToggleTime()
{
	/*CurrentToggleTime += 0.01f;

	
	if (CurrentToggleTime>=MaxToggleTime)
	{
		EndOrCancelSkill();
		return;
	}*/

	float Elapsed = (GetWorld()->GetTimeSeconds() - StartTime) * AttackSpeed;

	float FinalMaxToggleTime = MaxToggleTime * (1.f - (AttackSpeed - 1.f));

	if (Elapsed>= FinalMaxToggleTime)
	{
		EndOrCancelSkill();
		return;
	}

	CurrentToggleTime = Elapsed;

	FString TimeText = FString::Printf(TEXT("%.1fs"), CurrentToggleTime);
	ShowProcessBarFilling(TimeText, CurrentToggleTime, FinalMaxToggleTime);
}

void URPGToggleSkillAbility::StartSkill()
{
	//bIsActive = true;

	StartTime = GetWorld()->GetTimeSeconds();

	float Elapsed = (GetWorld()->GetTimeSeconds() - StartTime) * AttackSpeed;

	ShowProgressBar(CurrentActorInfo);

	PlaySkillMontage();

	GetWorld()->GetTimerManager().SetTimer(ToggleTimerHandle, this, &ThisClass::UpdateToggleTime, 0.01f, true);
}

void URPGToggleSkillAbility::EndOrCancelSkill()
{
	
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}