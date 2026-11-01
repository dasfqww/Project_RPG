// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/RPGComboSkillAbility.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/RPGBaseCharacter.h"
#include "Attribute/RPGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include <DataTable/SkillData.h>
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
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
	++ResolveDamageSerial;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ComboCountResetTimerHandle);
		ComboCountResetTimerHandle.Invalidate();
	}

	// Snapshot before any Blueprint/montage callbacks can produce a hit. Never
	// read a new encounter ID at impact time for an attack from an old encounter.
	CaptureBossEncounters(ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr);
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}

	const UAbilitySystemComponent* ASC =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const FGameplayAbilitySpec* Spec =
		ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	bComboInputHeld = Spec && Spec->InputPressed;
	bAdvanceBuffered = false;
	WaitForNextInputTransition();
	PlaySkillMontage();
	if (IsActive())
	{
		ExecWaitGameplayEvent();
	}
}

void URPGComboSkillAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	const uint64 EndingSerial = ++ResolveDamageSerial;
	ResolveCurrentStep = INDEX_NONE;
	if (bWasCancelled) { ResolveComboHitSteps.Reset(); ResolveStepCount = 0; }
	bComboInputHeld = false;
	bAdvanceBuffered = false;
	bHasNextComboSection = false;
	WaitInputPressTask = nullptr;
	WaitInputReleaseTask = nullptr;
	BossAttackEncounters.Reset();
	BossAttackSource.Reset();
	BossAttackController.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	if (GetWorld() && ResolveDamageSerial == EndingSerial && !IsActive())
	{
		GetWorld()->GetTimerManager().SetTimer(
			ComboCountResetTimerHandle,
			this,
			&ThisClass::ResetComboCount,
			TimeToTimerReset,
			false);
	}
}

void URPGComboSkillAbility::PlaySkillMontage()
{
	Super::PlaySkillMontage();

	//Debug::Print(TEXT("Current Combo Count"), CurrentComboCount);

	const FMultipleSectionData* SelectedAttackData = &DefaultAttackSectionData;

	//float AttackSpeed=CachedAttributeSet->GetAttackSpeed();

	//Debug::Print("Attack Speed: ", AttackSpeed);

	if (URPGAbilityFunctionLibrary::NativeDoesActorHaveTag
	(GetOwningActorFromActorInfo(), RPGGameplayTags::Player_Status_Rage_Active))
	{
		SelectedAttackData = &DefaultAttackSectionData_Rage;
	}

	const FName* StartSection =
		SelectedAttackData->SectionNamesToPlay.Find(CurrentComboCount);
	if (!SelectedAttackData->MontageToPlay || !StartSection ||
		!SelectedAttackData->MontageToPlay->IsValidSectionName(*StartSection))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	// The existing chain may continue across quick presses. Only a contiguous,
	// authored full chain from its first section can earn the completion credit.
	if (CurrentComboCount == InitComboCount || ResolveComboMontage.Get() != SelectedAttackData->MontageToPlay)
	{
		ResolveComboHitSteps.Reset();
		bResolveComboAwarded = false;
		ResolveComboMontage = SelectedAttackData->MontageToPlay;
		ResolveStepCount = SelectedAttackData->SectionNamesToPlay.Num();
		if (ResolveStepCount < 1 || ResolveStepCount > 32 || CurrentComboCount != InitComboCount) ResolveStepCount = 0;
		for (int32 Offset = 0; Offset < ResolveStepCount; ++Offset)
		{
			const auto* Section = SelectedAttackData->SectionNamesToPlay.Find(InitComboCount + Offset);
			if (!Section || !SelectedAttackData->MontageToPlay->IsValidSectionName(*Section)) { ResolveStepCount = 0; break; }
		}
	}
	ResolveCurrentStep = CurrentComboCount;

	UAbilityTask_PlayMontageAndWait* PlayAttackTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			SelectedAttackData->MontageToPlay,
			AttackSpeed,
			*StartSection
		);

	if (!PlayAttackTask)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	PlayAttackTask->OnCompleted.AddDynamic(this, &ThisClass::OnComboMontageCompleted);
	PlayAttackTask->OnInterrupted.AddDynamic(this, &URPGPlayerGameplayAbility::OnInterruptedCallback);
	bHasNextComboSection =
		SelectedAttackData->SectionNamesToPlay.Contains(CurrentComboCount + 1);
	CurrentComboCount = bHasNextComboSection
		? CurrentComboCount + 1
		: InitComboCount;
	PlayAttackTask->ReadyForActivation();
}

void URPGComboSkillAbility::OnComboMontageCompleted()
{
	if (!IsActive())
	{
		return;
	}
	const uint64 CompletionSerial = ResolveDamageSerial;
	if (!bHasNextComboSection && !bResolveComboAwarded && ResolveStepCount > 0
		&& ResolveComboHitSteps.Num() == ResolveStepCount)
	{
		bResolveComboAwarded = true;
		URPGGreatSwordResolveAbility::AwardCompletedBasicCombo(*this);
		if (!IsActive() || ResolveDamageSerial != CompletionSerial) return;
	}

	// This legacy montage has no combo-advance notify; its section end is the
	// authored transition point. Never restart the chain from a held input.
	if (bHasNextComboSection && (bComboInputHeld || bAdvanceBuffered))
	{
		bAdvanceBuffered = false;
		PlaySkillMontage();
		return;
	}

	OnCompleteCallBack();
}

void URPGComboSkillAbility::OnComboInputPressed(float TimeWaited)
{
	WaitInputPressTask = nullptr;
	if (!IsActive())
	{
		return;
	}

	bComboInputHeld = true;
	bAdvanceBuffered = true;
	WaitForNextInputTransition();
}

void URPGComboSkillAbility::OnComboInputReleased(float TimeHeld)
{
	WaitInputReleaseTask = nullptr;
	if (!IsActive())
	{
		return;
	}

	bComboInputHeld = false;
	WaitForNextInputTransition();
}

void URPGComboSkillAbility::WaitForNextInputTransition()
{
	if (bComboInputHeld)
	{
		WaitInputReleaseTask =
			UAbilityTask_WaitInputRelease::WaitInputRelease(this, false);
		if (WaitInputReleaseTask)
		{
			WaitInputReleaseTask->OnRelease.AddDynamic(
				this, &ThisClass::OnComboInputReleased);
			WaitInputReleaseTask->ReadyForActivation();
		}
	}
	else
	{
		WaitInputPressTask =
			UAbilityTask_WaitInputPress::WaitInputPress(this, false);
		if (WaitInputPressTask)
		{
			WaitInputPressTask->OnPress.AddDynamic(
				this, &ThisClass::OnComboInputPressed);
			WaitInputPressTask->ReadyForActivation();
		}
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
//	// SuccessType ���� ���� �б� ó��
//	if (SuccessType == ERPGSuccessType::Successful)
//	{
//		UE_LOG(LogTemp, Log, TEXT("Effect applied successfully."));
//
//		FGameplayEffectContextHandle EmptyContext;
//		K2_ExecuteGameplayCue(WeaponHitSoundCueTag, EmptyContext);
//
//		FGameplayEventData EventData;
//
//		// Instigator�� �����Ƽ �����ڷ� ����
//		if (CurrentActorInfo)
//		{
//			EventData.Instigator = CurrentActorInfo->OwnerActor.Get();
//		}
//
//		EventData.Target = CachedTargetActor;
//
//		// �̺�Ʈ ����(��Ʈ ���׼� �±� ����)
//		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(CachedTargetActor, HitReactTag, EventData);
//		
//		GainIdentity();
//	}
//	else
//	{
//		UE_LOG(LogTemp, Warning, TEXT("Effect application failed."));
//	}
//}

void URPGComboSkillAbility::CaptureBossEncounters(APawn* Source)
{
	BossAttackEncounters.Reset();
	BossAttackSource.Reset();
	BossAttackController.Reset();
	if (!Source || !Source->HasAuthority() || !Source->GetController() || !Source->GetWorld()) { return; }
	BossAttackSource = Source;
	BossAttackController = Source->GetController();
	for (TActorIterator<APawn> It(Source->GetWorld()); It; ++It)
	{
		auto* Boss = It->FindComponentByClass<URPGBossAIComponent>();
		auto* Threat = It->FindComponentByClass<URPGBossThreatComponent>();
		if (Boss && Threat && Boss->GetBossState().EncounterState == ERPGBossEncounterState::InCombat)
		{
			BossAttackEncounters.Add(Threat, Boss->GetBossState().EncounterId);
		}
	}
}

int32 URPGComboSkillAbility::GetCapturedBossEncounter(APawn* Source, URPGBossThreatComponent* Threat) const
{
	if (!Source || !Source->HasAuthority() || Source != BossAttackSource.Get() ||
		!BossAttackController.IsValid() || Source->GetController() != BossAttackController.Get()) { return 0; }
	const int32* Encounter = BossAttackEncounters.Find(Threat);
	return Encounter ? *Encounter : 0;
}

bool URPGComboSkillAbility::ApplyLegacyDamageToServerHit(const FHitResult& ServerHit,
	const FGameplayEffectSpecHandle& DamageSpec)
{
	const uint64 Serial = ResolveDamageSerial;
	const int32 Step = ResolveCurrentStep;
	AActor* Target = ServerHit.GetActor();
	auto* Threat = Target ? Target->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	if (!Threat)
	{
		const bool bApplied = Super::ApplyLegacyDamageToServerHit(ServerHit, DamageSpec);
		RecordResolveComboHit(Step, Serial, bApplied);
		return bApplied;
	}
	APawn* Source = Cast<APawn>(GetAvatarActorFromActorInfo());
	const int32 Encounter = GetCapturedBossEncounter(Source, Threat);
	UPlayerCombatComponent* Combat = GetPlayerCombatComponentFromActorInfo();
	ARPGPlayerWeapon* Weapon = Combat ? Combat->GetPlayerCurrentEquippedWeapon() : nullptr;
	UBoxComponent* Box = Weapon ? Weapon->GetWeaponCollisionBox() : nullptr;
	FHitResult ValidatedHit = ServerHit;
	const bool bServerSweep = Combat && Combat->TryGetCurrentServerWeaponHit(Target, ValidatedHit);
	if (!IsActive() || Encounter <= 0 || !Box || Box->GetCollisionEnabled() != ECollisionEnabled::QueryOnly ||
		(!Box->IsOverlappingActor(Target) && !bServerSweep)) { return false; }
	float HealthDamage = 0.f;
	bool bThreatAdded = false;
	const bool bApplied = Threat->ApplyDamageFromServerHit(Source, ValidatedHit, DamageSpec, Encounter,
		BuildLegacyDamageSecurityProfile(), HealthDamage, bThreatAdded);
	RecordResolveComboHit(Step, Serial, bApplied && HealthDamage > 0.f);
	return bApplied;
}

void URPGComboSkillAbility::RecordResolveComboHit(int32 Step, uint64 Serial, bool bApplied)
{
	if (bApplied && IsActive() && ResolveDamageSerial == Serial && ResolveCurrentStep == Step && ResolveStepCount > 0
		&& Step >= InitComboCount && Step - InitComboCount < ResolveStepCount
		&& URPGGreatSwordResolveAbility::IsConfiguredFor(GetAvatarActorFromActorInfo())) ResolveComboHitSteps.Add(Step);
}

void URPGComboSkillAbility::ResetComboCount()
{
	++ResolveDamageSerial;
	ResolveComboHitSteps.Reset(); ResolveStepCount = 0; ResolveCurrentStep = INDEX_NONE;
	ResolveComboMontage.Reset(); bResolveComboAwarded = false;
	CurrentComboCount = InitComboCount;
	//Debug::Print(TEXT("Current Combo Count Reset.."));
}
