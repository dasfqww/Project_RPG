// Fill out your copyright notice in the Description page of Project Settings.


#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillAction.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"

URPGGameplayAbility_SkillContainer::URPGGameplayAbility_SkillContainer()
{
	// 기본 설정
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void URPGGameplayAbility_SkillContainer::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!SkillDefinition)
	{
		UE_LOG(LogTemp, Error, TEXT("SkillDefinition is missing in %s"), *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 1. 현재 캐릭터 상태(태그) 및 트라이포드에 맞는 데이터 추출
	UTexture2D* SelectedIcon;
	UAnimMontage* SelectedMontage;
	TSubclassOf<URPGSkillAction> ActionClassToRun;

	TArray<int32> CurrentTripods;
	
	// SkillComponent에서 실제 저장된 데이터 가져오기
	if (URPGPlayerSkillComponent* SkillComp = GetAvatarActorFromActorInfo()->FindComponentByClass<URPGPlayerSkillComponent>())
	{
		FRPGSkillSaveData SaveData = SkillComp->GetSkillSaveData(SkillDefinition->SkillTag);
		CurrentTripods = SaveData.SelectedTripodIndices;
	}
	else
	{
		// 컴포넌트가 없으면(NPC 등) 기본값(-1) 사용
		CurrentTripods.Init(-1, 3);
	}

	SkillDefinition->GetSkillDataForContext(GetAvatarActorFromActorInfo(), CurrentTripods, SelectedIcon, SelectedMontage, ActionClassToRun);

	if (!ActionClassToRun)
	{
		UE_LOG(LogTemp, Error, TEXT("No ActionClass found for Definition %s in current state"), *SkillDefinition->GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 2. 액션 인스턴스 생성 (이 어빌리티를 Outer로)
	ActiveAction = NewObject<URPGSkillAction>(this, ActionClassToRun);

	if (ActiveAction)
	{
		// 3. 초기화 및 실행
		ActiveAction->Initialize(this, SkillDefinition);
		ActiveAction->StartAction();
	}
	else
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void URPGGameplayAbility_SkillContainer::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActiveAction)
	{
		// 액션 강제 종료 (이미 끝났으면 내부에서 무시)
		if (bWasCancelled)
		{
			ActiveAction->CancelAction();
		}
		else
		{
			ActiveAction->EndAction();
		}
		ActiveAction = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGGameplayAbility_SkillContainer::CancelAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateCancel)
{
	// Super::CancelAbility 호출 시 EndAbility(Cancelled=true)가 호출됨
	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancel);
}

void URPGGameplayAbility_SkillContainer::OnActionEnded()
{
	// 액션이 정상적으로 끝났다고 보고하면 어빌리티 종료
	bool bReplicateEndAbility = true;
	bool bWasCancelled = false;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicateEndAbility, bWasCancelled);
}
