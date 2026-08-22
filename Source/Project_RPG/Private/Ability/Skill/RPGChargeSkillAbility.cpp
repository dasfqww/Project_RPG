// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Skill/RPGChargeSkillAbility.h"
#include "Interface/PawnUIInterface.h"
#include "Component/UI/PawnUIComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Character/RPGPlayer.h"
#include "NiagaraComponent.h"

#include "RPGDebugHelper.h"
#include <NiagaraFunctionLibrary.h>
#include "RPGGameplayTags.h"
#include <Abilities/Tasks/AbilityTask_WaitGameplayEvent.h>

URPGChargeSkillAbility::URPGChargeSkillAbility():
	ChargeTime(0.f)
{
	//OnChargeLevelChanged.AddUObject(this, &URPGChargeSkillAbility::HandleChargeLevelChanged);
}

void URPGChargeSkillAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 캐릭터가 플레이어인지 확인 (보통 ACharacter 타입)
	ARPGPlayer* PlayerCharacter = Cast<ARPGPlayer>(ActorInfo->OwnerActor);

	if (PlayerCharacter)
	{
		// 플레이어 캐릭터의 씬 컴포넌트 가져오기 (예: NS_SceneComponent)
		USceneComponent* PlayerSceneComponent = PlayerCharacter->GetNS_SceneComponent();

		if (PlayerSceneComponent)
		{
			// Niagara 시스템이 이미 생성되었는지 확인
			if (!NiagaraComp || !NiagaraComp->IsActive())
			{
				// Niagara 시스템 생성
				NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
					ChargeNS,                       // 사용하고 싶은 Niagara 시스템
					PlayerSceneComponent,           // 부착할 씬 컴포넌트
					NAME_None,                      // Attach Name (보통은 None)
					FVector::ZeroVector,            // 상대적인 위치
					FRotator::ZeroRotator,          // 상대적인 회전
					EAttachLocation::KeepRelativeOffset, // 부착 위치 유지
					true,                           // 활성화
					true                            // 스폰 후 자동으로 제거
				);

				// Niagara 변수 설정
				if (NiagaraComp)
				{
					NiagaraComp->SetVariableBool(FName(TEXT("AddDetail")), false);
					NiagaraComp->SetVariableBool(FName(TEXT("Simple")), true);
				}
			}
		}
	}

	ChargeSkill();
	CoolDown();
}

void URPGChargeSkillAbility::CancelAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateCancel)
{
	//Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancel);

	HiddenProgressBar(ActorInfo);

	FName PrepareSection = *MultipleSectionData.SectionNamesToPlay.Find(1);

	PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			MultipleSectionData.MontageToPlay,
			AttackSpeed,
			PrepareSection
		);

	PlayAttackTask->OnCompleted.AddDynamic(this, &URPGPlayerGameplayAbility::OnCompleteCallBack);
	PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	PlayAttackTask->ReadyForActivation();

	ExecWaitGameplayEvent();

	CurrentChargeLevel = 0;
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
}

void URPGChargeSkillAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// 플레이어 캐릭터 가져오기
	ARPGBaseCharacter* PlayerCharacter = Cast<ARPGBaseCharacter>(ActorInfo->OwnerActor);

	if (PlayerCharacter && PlayerCharacter->GetNS_SceneComponent())
	{
		// Niagara 컴포넌트를 씬 컴포넌트 자식으로 가져오기
		USceneComponent* SceneComp = PlayerCharacter->GetNS_SceneComponent();
		if (SceneComp)
		{
			NiagaraComp = nullptr;

			// 자식 컴포넌트 중 NiagaraComponent를 찾아서 캐스팅
			for (USceneComponent* Child : SceneComp->GetAttachChildren())
			{
				NiagaraComp = Cast<UNiagaraComponent>(Child);
				if (NiagaraComp)
				{
					// 해당 Niagara 컴포넌트가 발견되었으면 비활성화
					//NiagaraComp->Deactivate();  // 비활성화
					//Debug::Print("niagara disabled..");
					NiagaraComp->DestroyComponent();  // 완전 제거 (원할 경우 이 라인 사용)
					break; // 더 이상 자식 컴포넌트를 찾지 않음
				}
			}
		}
	}

	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	//Debug::Print("end charge and execSkill..");
}

void URPGChargeSkillAbility::PlaySkillMontage()
{
	Super::PlaySkillMontage();

	FName PrepareSection = *MultipleSectionData.SectionNamesToPlay.Find(0);

	PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			MultipleSectionData.MontageToPlay,
			AttackSpeed,
			PrepareSection
		);

	//PlayAttackTask->OnCompleted.AddDynamic(this, &URPGPlayerGameplayAbility::OnCompleteCallBack);
	//PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	PlayAttackTask->ReadyForActivation();
}

void URPGChargeSkillAbility::ChargeSkill()
{
	StartTime = GetWorld()->GetTimeSeconds();

	ShowProgressBar(CurrentActorInfo);

	PlaySkillMontage();

	ChargeTime = 0.f;

	//Debug::Print("Activate charge..");
	GetWorld()->GetTimerManager().SetTimer(ChargeTimerHandle,
		this, &URPGChargeSkillAbility::UpdateChargeTime, 0.01f, true);
}

void URPGChargeSkillAbility::UpdateChargeTime()
{
	float Elapsed = (GetWorld()->GetTimeSeconds() - StartTime) * AttackSpeed;

    ChargeTime = Elapsed; // 타이머 주기에 맞게 증가

	FString TimeText = FString::Printf(TEXT("%.1fs"), ChargeTime);

    //Debug::Print(TEXT("ChargeTime: "), ChargeTime);

	float FinalChargeTimePerLevel = ChargeTimePerLevel * (1.f - (AttackSpeed - 1.f));

	if (ChargeTime >= FinalChargeTimePerLevel)
	{
		if (CurrentChargeLevel < MaxChargeLevel)
		{
			CurrentChargeLevel++;
			OnChargeLevelChanged.Broadcast(CurrentChargeLevel);
			Debug::Print("Charge level up!");

			if (CurrentChargeLevel == MaxChargeLevel)
			{
				ChargeTime = FinalChargeTimePerLevel;
				Debug::Print("Overcharge Ready!");
			}
			else
			{
				StartTime = GetWorld()->GetTimeSeconds();
				ChargeTime = 0.f;
			}
		}
	}
	
	ShowProcessBarFilling(TimeText, ChargeTime, FinalChargeTimePerLevel);

    // 최대 차지 단계에서 완료 처리
    if (CurrentChargeLevel == MaxChargeLevel)
    {
        OnChargeCompleted();
    }
}


void URPGChargeSkillAbility::OnChargeCompleted()
{
	Debug::Print("ChargeCompleted..");
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);

	// AbilityActorInfo를 사용하여 ActorInfo 가져오기
	ProgessCompleted();

	//TODO:오버차지 상태가 되었다면 몇 초후 강제로 EndAbility를 호출할것.

	float OverchargeDuration = 2.0f;
	GetWorld()->GetTimerManager().SetTimer(ChargeTimerHandle,
		this, &URPGChargeSkillAbility::OnOverchargeExpired, OverchargeDuration, false);

	//EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void URPGChargeSkillAbility::OnOverchargeExpired()
{
	//Debug::Print("Overcharge expired. Ending ability...");

	ChargeTime = 0.f;

	FName PrepareSection = *MultipleSectionData.SectionNamesToPlay.Find(1);

	PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			MultipleSectionData.MontageToPlay,
			AttackSpeed,
			PrepareSection
		);

	PlayAttackTask->OnCompleted.AddDynamic(this, &URPGPlayerGameplayAbility::OnCompleteCallBack);
	PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	PlayAttackTask->ReadyForActivation();

	ExecWaitGameplayEvent();

	//EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	//CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void URPGChargeSkillAbility::HandleChargeLevelChanged(int32 Chargelevel)
{
	// TMap에서 해당 차지 단계의 데이터를 가져옵니다.
	if (ChargeLevelSettings.Contains(Chargelevel))
	{
		FChargeLevelNiagaraOptionData& LevelData = ChargeLevelSettings[Chargelevel];

		if (NiagaraComp)
		{
			NiagaraComp->SetVariableBool(FName(TEXT("AddDetail")), LevelData.bAddDetail);
			NiagaraComp->SetVariableBool(FName(TEXT("Simple")), LevelData.bSimple);
		}
	}
}
