// Fill out your copyright notice in the Description page of Project Settings.


#include "Skill/RPGSkillAction.h"
#include "Ability/RPGGameplayAbility.h"
#include "Skill/RPGSkillDefinition.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "Component/RPGAbilitySystemComponent.h"

URPGSkillAction::URPGSkillAction()
{
	bIsActive = false;
}

void URPGSkillAction::Initialize(URPGGameplayAbility* InAbility, URPGSkillDefinition* InDefinition)
{
	OwnerAbility = InAbility;
	SkillDefinition = InDefinition;
}

void URPGSkillAction::StartAction()
{
	bIsActive = true;
	// 자식 클래스에서 구체적인 로직 구현 (예: 타이머 시작, 몽타주 재생 등)
}

void URPGSkillAction::EndAction()
{
	bIsActive = false;
	
	// Ability에게 종료를 알림
	// if (OwnerAbility) OwnerAbility->OnActionEnded(); 
}

void URPGSkillAction::CancelAction()
{
	bIsActive = false;
	// 타이머 해제 등 정리 작업
}

void URPGSkillAction::TickAction(float DeltaTime)
{
	// 필요시 구현
}

ACharacter* URPGSkillAction::GetCharacter() const
{
	if (OwnerAbility)
	{
		return Cast<ACharacter>(OwnerAbility->GetAvatarActorFromActorInfo());
	}
	return nullptr;
}

URPGAbilitySystemComponent* URPGSkillAction::GetAbilitySystemComponent() const
{
	if (OwnerAbility)
	{
		return Cast<URPGAbilitySystemComponent>(OwnerAbility->GetAbilitySystemComponentFromActorInfo());
	}
	return nullptr;
}

UWorld* URPGSkillAction::GetWorld() const
{
	if (OwnerAbility)
	{
		return OwnerAbility->GetWorld();
	}
	return nullptr;
}

void URPGSkillAction::PlayMontage()
{
	// Definition에 있는 몽타주를 재생하는 편의 함수 구현 예정
}
