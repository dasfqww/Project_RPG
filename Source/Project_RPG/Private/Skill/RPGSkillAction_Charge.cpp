// Fill out your copyright notice in the Description page of Project Settings.


#include "Skill/RPGSkillAction_Charge.h"
#include "Skill/RPGSkillDefinition_Charge.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Character/RPGPlayer.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "RPGDebugHelper.h"
#include "Component/UI/PlayerUIComponent.h"

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
	if (Player && RuntimeSpec.VFX)
	{
		NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			RuntimeSpec.VFX,
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
	bReleased = false;

	BeginProgress();
	if (!bIsActive) return;
	PlayMontageSection(0);
	if (!bIsActive) return;

	GetWorld()->GetTimerManager().SetTimer(ChargeTimerHandle, this, &URPGSkillAction_Charge::UpdateChargeTime, 0.01f, true);
}

void URPGSkillAction_Charge::UpdateChargeTime()
{
	if (!bIsActive || bReleased) return;
	URPGSkillDefinition_Charge* ChargeDef = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility);
	if (!ChargeDef || !Container) return;

	float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
	ChargeTime = Elapsed;

	static const FGameplayTag ChargeTimeStatTag =
		FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.ChargeTime"), false);
	const float ChargeTimeScalar = RuntimeSpec.GetStatScalar(ChargeTimeStatTag);
	const float FinalChargeTimePerLevel =
		FMath::Max(0.01f, ChargeDef->ChargeTimePerLevel * ChargeTimeScalar);

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

	PublishProgress(CurrentChargeLevel == ChargeDef->MaxChargeLevel);
}

void URPGSkillAction_Charge::OnChargeCompleted()
{
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	
	PublishProgress(true);
	if (!bIsActive) return;

	// 오버차지 대기 타이머 (로아 스타일: 풀차지 후 일정 시간 유지 가능)
	float OverchargeDuration = 2.0f;
	GetWorld()->GetTimerManager().SetTimer(ChargeTimerHandle, this, &URPGSkillAction_Charge::OnOverchargeExpired, OverchargeDuration, false);
}

void URPGSkillAction_Charge::OnOverchargeExpired()
{
	if (!bIsActive || bReleased) return;
	bReleased = true;
	EndProgress(ERPGActionProgressEndReason::Completed);
	if (!bIsActive) return;
	PlayMontageSection(1); // 발사 섹션
	
	if (URPGGameplayAbility_SkillContainer* Container = Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility))
	{
		Container->ExecuteWaitEvent_Internal();
	}
}

void URPGSkillAction_Charge::CancelAction()
{
	Super::CancelAction();
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ChargeTimerHandle);
	
	if (NiagaraComp) NiagaraComp->DestroyComponent();
	
	EndProgress(ERPGActionProgressEndReason::Cancelled);
}

void URPGSkillAction_Charge::EndAction()
{
	Super::EndAction();
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ChargeTimerHandle);

	if (NiagaraComp) NiagaraComp->DestroyComponent();
	
	EndProgress(ERPGActionProgressEndReason::Completed);
}

void URPGSkillAction_Charge::OnInputReleased()
{
	if (!bIsActive || bReleased) return;
	bReleased = true;
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	EndProgress(ERPGActionProgressEndReason::Completed);
	if (!bIsActive) return;
	PlayMontageSection(1);

	if (URPGGameplayAbility_SkillContainer* Container =
		Cast<URPGGameplayAbility_SkillContainer>(OwnerAbility))
	{
		Container->ExecuteWaitEvent_Internal();
	}
}

void URPGSkillAction_Charge::BeginProgress()
{
	EndProgress(ERPGActionProgressEndReason::Cancelled);
	auto* Character = GetCharacter();
	if (!bIsActive || !Character || !Character->IsLocallyControlled() || Character->GetNetMode() == NM_DedicatedServer) return;
	auto* UI = Character->FindComponentByClass<UPlayerUIComponent>();
	const auto* Definition = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	if (!UI || !Definition) return;
	static const auto TimeTag = FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.ChargeTime"), false);
	const float Scalar = RuntimeSpec.GetStatScalar(TimeTag);
	ProgressData = {};
	ProgressData.Kind = ERPGActionProgressKind::Charge;
	ProgressData.Label = RuntimeSpec.DisplayName.IsEmpty() ? Definition->SkillName : RuntimeSpec.DisplayName;
	ProgressData.Icon = RuntimeSpec.Icon;
	ProgressData.bHasDuration = true;
	ProgressData.DurationSeconds = FMath::Max(.01f, Definition->ChargeTimePerLevel * Scalar) * Definition->MaxChargeLevel;
	ProgressData.TimeDisplay = ERPGActionProgressTimeDisplay::Elapsed;
	ProgressData.StageCount = Definition->MaxChargeLevel;
	ProgressUI = UI;
	const uint64 Revision = ProgressRevision;
	const auto Handle = UI->BeginActionProgress(this, ProgressData);
	if (Revision != ProgressRevision || !bIsActive)
	{
		UI->EndActionProgress(Handle, ERPGActionProgressEndReason::Cancelled);
		return;
	}
	ProgressHandle = Handle;
}

void URPGSkillAction_Charge::PublishProgress(bool bReady)
{
	if (!bIsActive || bReleased) return;
	const float PerLevel = ProgressData.StageCount > 0 ? ProgressData.DurationSeconds / ProgressData.StageCount : 0.f;
	ProgressData.ElapsedSeconds = bReady ? ProgressData.DurationSeconds : CurrentChargeLevel * PerLevel + ChargeTime;
	ProgressData.Stage = CurrentChargeLevel;
	ProgressData.Phase = bReady ? ERPGActionProgressPhase::Ready : ERPGActionProgressPhase::Running;
	if (auto* UI = ProgressUI.Get()) UI->UpdateActionProgress(ProgressHandle, ProgressData);
}

void URPGSkillAction_Charge::EndProgress(ERPGActionProgressEndReason Reason)
{
	++ProgressRevision;
	auto* UI = ProgressUI.Get();
	const auto Handle = ProgressHandle;
	ProgressUI.Reset(); ProgressHandle = {}; ProgressData = {};
	if (IsValid(UI)) UI->EndActionProgress(Handle, Reason);
}

void URPGSkillAction_Charge::HandleChargeLevelChanged(int32 NewLevel)
{
	URPGSkillDefinition_Charge* ChargeDef = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	if (!ChargeDef || !NiagaraComp) return;

	if (ChargeDef->ChargeLevelSettings.Contains(NewLevel))
	{
		const FChargeLevelNiagaraOptionData& Data = ChargeDef->ChargeLevelSettings[NewLevel];
		NiagaraComp->SetVariableBool(FName(TEXT("AddDetail")), Data.bAddDetail);
		NiagaraComp->SetVariableBool(FName(TEXT("Simple")), Data.bSimple);
	}
	Debug::Print(TEXT("New Charge Level: "), NewLevel);
}

void URPGSkillAction_Charge::PlayMontageSection(int32 SectionIndex)
{
	URPGSkillDefinition_Charge* ChargeDef = Cast<URPGSkillDefinition_Charge>(SkillDefinition);
	if (!ChargeDef || !RuntimeSpec.Montage) return;

	FName SectionName = NAME_None;
	if (ChargeDef->MontageSections.SectionNamesToPlay.Contains(SectionIndex))
	{
		SectionName = ChargeDef->MontageSections.SectionNamesToPlay[SectionIndex];
	}

	// 첫 번째 섹션(보통 루프/차지 중)이 아닐 때만 완료 콜백을 등록하여 어빌리티를 종료시킴
	PlayAttackTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		OwnerAbility,
		NAME_None,
		RuntimeSpec.Montage,
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
