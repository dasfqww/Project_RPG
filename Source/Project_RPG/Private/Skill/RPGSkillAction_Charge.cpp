// Fill out your copyright notice in the Description page of Project Settings.


#include "Skill/RPGSkillAction_Charge.h"
#include "Skill/RPGSkillDefinition_Charge.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Character/RPGPlayer.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "RPGDebugHelper.h"

URPGSkillAction_Charge::URPGSkillAction_Charge()
{
}

void URPGSkillAction_Charge::StartAction()
{
	Super::StartAction();

	URPGSkillDefinition_Charge* ChargeDef = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility);
	
	if (!ChargeDef || !Container) return;

	ARPGPlayer* Player = Cast<ARPGPlayer>(GetCharacter());
	if (Player && ChargeDef->SkillVFX)
	{
		NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			ChargeDef->SkillVFX,
			Player->GetNS_SceneComponent(),
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			true
		);
	}

	StartTime = GetWorld()->GetTimeSeconds();
	CurrentChargeLevel = 0;
	ChargeTime = 0.f;

	Container->ShowProgressBar_Internal();
	PlayMontageSection(0);

	GetWorld()->GetTimerManager().SetTimer(ChargeTimerHandle, this, &URPGSkillAction_Charge::UpdateChargeTime, 0.01f, true);
}

void URPGSkillAction_Charge::UpdateChargeTime()
{
	URPGSkillDefinition_Charge* ChargeDef = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility);
	if (!ChargeDef || !Container) return;

	float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
	ChargeTime = Elapsed;

	float FinalChargeTimePerLevel = ChargeDef->ChargeTimePerLevel; // 원래는 공속 계산 로직 필요
	FString TimeText = FString::Printf(TEXT("%.1fs"), ChargeTime);

	if (ChargeTime >= FinalChargeTimePerLevel)
	{
		if (CurrentChargeLevel < ChargeDef->MaxChargeLevel)
		{
			CurrentChargeLevel++;
			HandleChargeLevelChanged(CurrentChargeLevel);
			
			if (CurrentChargeLevel == ChargeDef->MaxChargeLevel)
			{
				ChargeTime = FinalChargeTimePerLevel;
				OnChargeCompleted();
			}
			else
			{
				StartTime = GetWorld()->GetTimeSeconds();
				ChargeTime = 0.f;
			}
		}
	}

	Container->UpdateProgressBar_Internal(TimeText, ChargeTime, FinalChargeTimePerLevel);
}

void URPGSkillAction_Charge::OnChargeCompleted()
{
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	
	if (URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility))
	{
		Container->ProgressCompleted_Internal();
	}

	// 오버차지 대기 타이머 (로아 스타일: 풀차지 후 일정 시간 유지 가능)
	float OverchargeDuration = 2.0f;
	GetWorld()->GetTimerManager().SetTimer(ChargeTimerHandle, this, &URPGSkillAction_Charge::OnOverchargeExpired, OverchargeDuration, false);
}

void URPGSkillAction_Charge::OnOverchargeExpired()
{
	PlayMontageSection(1); // 발사 섹션
	
	if (URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility))
	{
		Container->ExecuteWaitEvent_Internal();
	}
}

void URPGSkillAction_Charge::CancelAction()
{
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	
	if (NiagaraComp) NiagaraComp->DestroyComponent();
	
	if (URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility))
	{
		Container->HiddenProgressBar_Internal();
	}

	Super::CancelAction();
}

void URPGSkillAction_Charge::EndAction()
{
	if (NiagaraComp) NiagaraComp->DestroyComponent();
	
	if (URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility))
	{
		Container->HiddenProgressBar_Internal();
	}

	Super::EndAction();
}

void URPGSkillAction_Charge::HandleChargeLevelChanged(int32 NewLevel)
{
	URPGSkillDefinition_Charge* ChargeDef = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	if (!ChargeDef || !NiagaraComp) return;

	if (ChargeDef->ChargeLevelSettings.Contains(NewLevel))
	{
		const FChargeLevelNiagaraOptionData& Data = ChargeDef->ChargeLevelSettings[NewLevel];
		NiagaraComp->SetNiagaraVariableBool(TEXT("AddDetail"), Data.bAddDetail);
		NiagaraComp->SetNiagaraVariableBool(TEXT("Simple"), Data.bSimple);
	}
	Debug::Print(TEXT("New Charge Level: "), NewLevel);
}

void URPGSkillAction_Charge::PlayMontageSection(int32 SectionIndex)
{
	URPGSkillDefinition_Charge* ChargeDef = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	if (!ChargeDef || !ChargeDef->SkillMontage) return;

	FName SectionName = NAME_None;
	if (ChargeDef->MontageSections.SectionNamesToPlay.Contains(SectionIndex))
	{
		SectionName = ChargeDef->MontageSections.SectionNamesToPlay[SectionIndex];
	}

	// 첫 번째 섹션(보통 루프/차지 중)이 아닐 때만 완료 콜백을 등록하여 어빌리티를 종료시킴
	PlayAttackTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		OwnerAbility,
		NAME_None,
		ChargeDef->SkillMontage,
		1.0f,
		SectionName
	);
	
	if (SectionIndex != 0) 
	{
		PlayAttackTask->OnCompleted.AddDynamic(Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility), &URPGGameplayAbility_SkillContainer::OnActionEnded);
		PlayAttackTask->OnInterrupted.AddDynamic(Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility), &URPGGameplayAbility_SkillContainer::OnActionEnded);
	}

	PlayAttackTask->ReadyForActivation();
}
