// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/RPGPlayerGameplayAbility.h"
#include "Character/RPGPlayer.h"
#include "Controller/RPGPlayerController.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "DataTable/SkillData.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "Manager/SoundManager.h"
#include "GameInstance/RPGGameInstance.h"
#include "NiagaraFunctionLibrary.h"

#include "RPGGameplayTags.h"
#include "RPGDebugHelper.h"

bool URPGPlayerGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, 
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	// �θ� Ŭ������ �⺻ ���� �˻�
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;  // �θ� Ŭ�������� ������ �������� ������ �ٷ� false ��ȯ
	}

	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();

		if (ASC)
		{
			// AbilitySystemComponent�� ����� ��� �±׸� ��������
			const FGameplayTagContainer& AppliedTags = ASC->GetOwnedGameplayTags();

			// �±׸� ���������� ���
			for (const FGameplayTag& Tag : AppliedTags)
			{
				UE_LOG(LogTemp, Warning, TEXT("Applied Tag: %s"), *Tag.ToString());
			}
		}
		
		// ���� ���� üũ: Player.Status.Rage.Activating �±װ� �ִ��� Ȯ��
		if (AbilityTags.HasTag(FGameplayTag::RequestGameplayTag(FName("Player.Ability.IdentitySkill"))))
		{
			// IdentitySkill �±װ� �̹� ���� ���� ���� �Ұ�
			if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("Player.Status.Rage.Active"))))
			{
				return true;  // �̹� IdentitySkill �±װ� ������ ��� �Ұ�
			}

			else
			{
				return false;
			}
		}		
	}

	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo->AbilitySystemComponent.Get();
	const URPGAttributeSet* AttributeSet = AbilitySystemComponent->GetSet<URPGAttributeSet>();

	if (AttributeSet)
	{
		float CurrentMana = AttributeSet->GetCurrentMana();       // ���� ���� ��������
		float ManaCost = RequireManaCost;                    // ���� �Ҹ� �������� (�Լ��� ���� ����)

		if (ManaCost > 0.0f && CurrentMana < ManaCost)
		{
			UE_LOG(LogTemp, Warning, TEXT("Not enough mana!"));  // ���� ���� �޽��� ���
			return false;  // ������ �����ϸ� Ȱ��ȭ �Ұ�
		}
	}

	return true;  // ��� ������ �����Ǹ� Ȱ��ȭ
}

void URPGPlayerGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!ApplyManaCost(ActorInfo)) // ���� �Ҹ� ���� ��
	{
		UE_LOG(LogTemp, Warning, TEXT("Not enough mana to activate ability."));

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true); // �����Ƽ ����
		return;
	}

	if (!CachedAttributeSet)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s] ActivateAbility() - CachedAttributeSet is NULL! Trying to reinitialize..."), *GetName());

		// ������ �ٽ� �ʱ�ȭ
		if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
		{
			CachedAttributeSet = ActorInfo->AbilitySystemComponent->GetSet<URPGAttributeSet>();
		}

		if (!CachedAttributeSet)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] ActivateAbility() - CachedAttributeSet is STILL NULL!"), *GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s] ActivateAbility() - CachedAttributeSet Reinitialized Successfully!"), *GetName());
		}
	}

	

	AttackSpeed = CachedAttributeSet->GetAttackSpeed();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void URPGPlayerGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);


}

void URPGPlayerGameplayAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);

	LoadSkillData(SkillName);
}

ARPGPlayer* URPGPlayerGameplayAbility::GetPlayerCharacterFromActorInfo()
{
	if (!CachedPlayer.IsValid())
	{
		CachedPlayer = Cast<ARPGPlayer>(CurrentActorInfo->AvatarActor);
	}

	return CachedPlayer.IsValid() ? CachedPlayer.Get() : nullptr;
}

ARPGPlayerController* URPGPlayerGameplayAbility::GetPlayerControllerFromActorInfo()
{
	if (!CachedPlayerController.IsValid())
	{
		CachedPlayerController = Cast<ARPGPlayerController>(CurrentActorInfo->PlayerController);
	}

	return CachedPlayerController.IsValid() ? CachedPlayerController.Get() : nullptr;
}

UPlayerCombatComponent* URPGPlayerGameplayAbility::GetPlayerCombatComponentFromActorInfo()
{
	return GetPlayerCharacterFromActorInfo()->GetPlayerCombatComponent();
}

FGameplayEffectSpecHandle URPGPlayerGameplayAbility::MakePlayerDamageEffectSpecHandle
	(TSubclassOf<UGameplayEffect> EffectClass, float InDamage)
{
	check(EffectClass);

	FGameplayEffectContextHandle ContextHandle = GetRPGAbilitySystemComponentFromActorInfo()->MakeEffectContext();
	ContextHandle.SetAbility(this);
	ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	FGameplayEffectSpecHandle EffectSpecHandle = GetRPGAbilitySystemComponentFromActorInfo()->MakeOutgoingSpec(
		EffectClass,
		GetAbilityLevel(),
		ContextHandle
	);

	EffectSpecHandle.Data->SetSetByCallerMagnitude(
		RPGGameplayTags::Shared_SetByCaller_BaseDamage,
		InDamage
	);

	return EffectSpecHandle;
}

void URPGPlayerGameplayAbility::OnCompleteCallBack()
{
	bool bReplicatedEndAbility = true;
	bool bWasCancelled = false;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicatedEndAbility, bWasCancelled);
}

void URPGPlayerGameplayAbility::OnInterruptedCallback()
{
	bool bReplicatedEndAbility = true;
	bool bWasCancelled = true;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicatedEndAbility, bWasCancelled);
}

void URPGPlayerGameplayAbility::PlaySkillMontage()
{
	
}

//float URPGPlayerGameplayAbility::CalculateCriticalDamage(AActor* InCachedTargetActor, float InDamage)
//{
//	//CachedAttributeSet = GetAbilitySystemComponent()->GetSet<URPGAttributeSet>();
//
//	float CriticalChance = CachedAttributeSet->GetCriticalChance();
//	float CriticalDamageMultiplier = CachedAttributeSet->GetCriticalDamage();
//
//	// ���� Ȯ���� ġ��Ÿ ���� �Ǵ�
//	float RandomChance = FMath::RandRange(0.0f, 1.0f);
//	if (RandomChance <= CriticalChance)
//	{
//		// ġ��Ÿ �߻� �� ������ ���� ����
//		InDamage *= CriticalDamageMultiplier;
//
//		Debug::Print("Calc Damage: ", InDamage);
//
//		//DisplayDamageEffect(InCachedTargetActor, InDamage, true);
//	}
//	else
//	{
//		//DisplayDamageEffect(InCachedTargetActor, InDamage, false);
//	}
//
//	return InDamage;
//}

void URPGPlayerGameplayAbility::ExecWaitGameplayEvent()
{
	/*if (IsPendingKillEnabled())
	{
		UE_LOG(LogTemp, Error, TEXT("URPGPlayerGameplayAbility is not valid or is pending kill!"));
		return;
	}*/

	// ���� �½�ũ ���� (�ߺ� ��� ����)
	if (WaitGameplayEventTask)
	{
		WaitGameplayEventTask->EndTask();
		WaitGameplayEventTask = nullptr;
	}


	WaitGameplayEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		EventTag,
		nullptr,
		false,  // bOnlyTriggerOnce: �̺�Ʈ�� �� ���� Ʈ���ŵ��� ����
		true   // bMatchExact: �±װ� ��Ȯ�� ��ġ�ؾ� �ϴ��� ����
	);


	if (WaitGameplayEventTask)
	{
		WaitGameplayEventTask->EventReceived.AddDynamic(this, &URPGPlayerGameplayAbility::OnGameplayEventReceived);

		WaitGameplayEventTask->ReadyForActivation();
	}
}

void URPGPlayerGameplayAbility::OnGameplayEventReceived(FGameplayEventData Payload)
{
	/*UE_LOG(LogTemp, Log, TEXT("GameplayEvent Received!"));
	UE_LOG(LogTemp, Log, TEXT("EventTag: %s"), *Payload.EventTag.ToString());
	UE_LOG(LogTemp, Log, TEXT("Instigator: %s"), *GetNameSafe(Payload.Instigator));
	UE_LOG(LogTemp, Log, TEXT("Target: %s"), *GetNameSafe(Payload.Target));
	UE_LOG(LogTemp, Log, TEXT("OptionalObject: %s"), *GetNameSafe(Payload.OptionalObject));
	UE_LOG(LogTemp, Log, TEXT("ContextHandle: %s"), *Payload.ContextHandle.ToString());
	UE_LOG(LogTemp, Log, TEXT("InstigatorTags: %s"), *Payload.InstigatorTags.ToStringSimple());
	UE_LOG(LogTemp, Log, TEXT("TargetTags: %s"), *Payload.TargetTags.ToStringSimple());
	UE_LOG(LogTemp, Log, TEXT("EventMagnitude: %f"), Payload.EventMagnitude);*/

	if (Payload.EventTag==RPGGameplayTags::Shared_Event_Hit)
	{
		HandleApplyDamage(Payload);
	}

	else if (Payload.EventTag==RPGGameplayTags::Player_Event_AOE)
	{
		HandleApplyAOEDamage(Payload);
	}
}

void URPGPlayerGameplayAbility::LoadSkillData(const FName& SkillRowName)
{
	if (UWorld* World = GetWorld())
	{
		if (URPGGameInstance* GameInstance = Cast<URPGGameInstance>(World->GetGameInstance()))
		{
			const FRPGSkillDataTable* SkillRow = GameInstance->GetSkillData(SkillRowName);
			if (SkillRow)
			{
				RequireManaCost = SkillRow->ManaCost;
				SkillDamage = SkillRow->AttackPower;
				GainIdentityAmount = SkillRow->IdentityGainAmount;
			}
		}
	}	
}

void URPGPlayerGameplayAbility::HandleApplyDamage(const FGameplayEventData& InGameplayEventData)
{
	//Super::HandleApplyDamage(InGameplayEventData);

	AActor* CachedTargetActor = const_cast<AActor*>(InGameplayEventData.Target.Get());

	if (URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(CachedTargetActor, RPGGameplayTags::Shared_Status_Invincible))
	{
		DisplayInvincibleEffect(CachedTargetActor);

		return;
	}

	float Damage = SkillDamage;

	//float CalcDamage = CalculateCriticalDamage(CachedTargetActor, Damage);

	FGameplayEffectSpecHandle GameplayEffectSpecHandle;
	GameplayEffectSpecHandle =
		MakePlayerDamageEffectSpecHandle(DamageEffectClass, Damage);

	//ERPGSuccessType SuccessType;

	NativeApplyEffectSpecHandleToTarget(CachedTargetActor, GameplayEffectSpecHandle);

	UE_LOG(LogTemp, Log, TEXT("Effect applied successfully."));

	FGameplayEffectContextHandle EmptyContext;
	K2_ExecuteGameplayCue(WeaponHitSoundCueTag, EmptyContext);

	FGameplayEventData EventData;

	// Instigator�� �����Ƽ �����ڷ� ����
	if (CurrentActorInfo)
	{
		EventData.Instigator = CurrentActorInfo->OwnerActor.Get();
	}

	EventData.Target = CachedTargetActor;

	// �̺�Ʈ ����(��Ʈ ���׼� �±� ����)
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(CachedTargetActor, HitReactTag, EventData);

	GainIdentity();
}

void URPGPlayerGameplayAbility::HandleApplyAOEDamage(const FGameplayEventData& InGameplayEventData)
{
	TArray<FHitResult> HitResults;

	FVector Origin = GetAvatarActorFromActorInfo()->GetActorLocation();
	float Radius = 200.f;  // ���� �ݰ�
	FVector HalfSize = FVector(200.f, 200.f, 100.f);
	FRotator Rotation = FRotator::ZeroRotator;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Pawn));  // Pawn Ÿ�Ը� üũ

	switch (AOETraceType)
	{
	case EAOETraceType::Box:
		HitResults = URPGCombatFunctionLibrary::DoBoxTrace(GetWorld(), Origin, HalfSize, Rotation
			,50.f, ObjectTypes);
		break;
	case EAOETraceType::Sphere:
		HitResults = URPGCombatFunctionLibrary::DoSphereTrace(GetWorld(),
			Origin, Radius, 50.f, ObjectTypes, GetAvatarActorFromActorInfo());
		break;
	case EAOETraceType::Cone:
		break;
	case EAOETraceType::Capsule:
		break;
	default:
		break;
	}

	float Damage =SkillDamage;

	//Debug::Print("AOE Damage: ", WeaponBaseDamage);

	FGameplayEffectSpecHandle GameplayEffectSpecHandle;

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (HitActor)
		{
			FGameplayEffectContextHandle EmptyContext;
			K2_ExecuteGameplayCue(WeaponHitSoundCueTag, EmptyContext);

			//float CalcDamage = CalculateCriticalDamage(HitActor, Damage);
			
			GameplayEffectSpecHandle =
				MakePlayerDamageEffectSpecHandle(DamageEffectClass, Damage);

			UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), HitEffect, HitActor->GetActorLocation());
		}
	}

	ApplyGameplayEffectSpecHandleToHitResults(GameplayEffectSpecHandle, HitResults);

	if (HitResults.Num()>0)
	{
		GainIdentity();
	}
}

void URPGPlayerGameplayAbility::GainIdentity()
{
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();

	// IdentitySkill �±װ� �̹� ���� ���� ���� �Ұ�
	if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("Player.Status.Rage.Active"))))
	{
		return;  // �̹� IdentitySkill �±װ� ������ ��� �Ұ�
	}
		
	float IdentityGainAmount = GainIdentityAmount;

	// �����÷��� ����Ʈ�� ���޷� ����
	FGameplayEffectSpecHandle EffectSpecHandle = MakeOutgoingGameplayEffectSpec(GainIdentityEffectClass, 1.0f);

	if (EffectSpecHandle.IsValid())
	{
		FGameplayEffectSpec* EffectSpec = EffectSpecHandle.Data.Get();
		if (EffectSpec)
		{
			// SetByCaller�� ���޷� ����
			FGameplayTag IdentityGainTag = 
				FGameplayTag::RequestGameplayTag(FName("Player.Ability.Skill.GainIdentity"));
			EffectSpec->SetSetByCallerMagnitude(IdentityGainTag, IdentityGainAmount);
		}

		// Effect�� ����
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, 
			CurrentActorInfo, CurrentActivationInfo, EffectSpecHandle);

	}
}

//FGameplayEffectSpecHandle URPGPlayerGameplayAbility::MakeCoolDownGameplayEffect()
//{
//	//float CooldownTime = CoolTime;
//
//	if (!CooldownGameplayEffectClass)  // UPROPERTY�� ����ص� ��ٿ� GE Ŭ����
//	{
//		UE_LOG(LogTemp, Warning, TEXT("CooldownGameplayEffectClass is null!"));
//		return nullptr;
//	}
//
//	// ���� ����
//	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CooldownGameplayEffectClass, GetAbilityLevel());
//
//	if (SpecHandle.IsValid())
//	{
//		// SetByCaller �� ���� (CoolTime�� �� Ability�� �ִ� float ����)
//		SpecHandle.Data->SetSetByCallerMagnitude(
//			FGameplayTag::RequestGameplayTag(FName("Player.Ability.Skill.CoolDown")),
//			CoolTime
//		);
//	}
//
//	return SpecHandle;
//}

void URPGPlayerGameplayAbility::CoolDown()
{
	CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo);

	GetPlayerCharacterFromActorInfo()->
		GetPlayerUIComponent()->
		OnAbilityCooldownBegin.Broadcast(
			InputTag, 
			GetCooldownTimeRemaining(),
			GetCooldownTimeRemaining());
}

bool URPGPlayerGameplayAbility::ApplyManaCost(const FGameplayAbilityActorInfo* ActorInfo)
{
	UAbilitySystemComponent* AbilitySystemComponent = ActorInfo->AbilitySystemComponent.Get();
	const URPGAttributeSet* AttributeSet = AbilitySystemComponent->GetSet<URPGAttributeSet>();

	if (AttributeSet)
	{
		float CurrentMana = AttributeSet->GetCurrentMana();
		float ManaCost = RequireManaCost;  // �����Ƽ���� �ٸ� ���� �Ҹ��� ������ �� �ִٰ� ����

		if (ManaCost <= 0.0f)  // ���� �Ҹ� 0�̰ų� ������� �׳� ���
		{
			return true;
		}

		if (CurrentMana >= ManaCost)
		{
			AbilitySystemComponent->ApplyModToAttribute(AttributeSet->GetCurrentManaAttribute(),
				EGameplayModOp::Additive, -ManaCost);

			// ActorInfo�κ��� PawnUIComponent�� �����ɴϴ�.
			if (ActorInfo)
			{
				// Pawn UI �������̽��� ĳ�����Ͽ� ���
				if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(ActorInfo->OwnerActor))
				{
					// UI ������Ʈ�� ����
					if (UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent())
					{
						// ���α׷��� �� UI�� ���̵��� ��������Ʈ ȣ��
						PlayerUIComponent->OnCurrentManaChanged.Broadcast
							(AttributeSet->GetCurrentMana()/AttributeSet->GetMaxMana());  // ���α׷��� �� ǥ��
						
						FString ManaText = FString::Printf(TEXT("%.0f/%.0f"),
							AttributeSet->GetCurrentMana(), AttributeSet->GetMaxMana());
						PlayerUIComponent->OnManaTextChanged.Broadcast(ManaText);
					}
				}
			}

			UE_LOG(LogTemp, Log, TEXT("Mana reduced by %f. Current Mana: %f"), ManaCost, CurrentMana - ManaCost);
			return true;  // ���� �Ҹ� ����
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Not enough mana! Current Mana: %f, Required Mana: %f"), CurrentMana, ManaCost);
		}
	}

	return false;  // ���� �Ҹ� ����
}

void URPGPlayerGameplayAbility::ShowProgressBar(const FGameplayAbilityActorInfo* ActorInfo)
{
	if (ActorInfo)
	{
		// Pawn UI �������̽��� ĳ�����Ͽ� ���
		if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(ActorInfo->OwnerActor))
		{
			// UI ������Ʈ�� ����
			if (UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent())
			{
				// ���α׷��� �� UI�� ���̵��� ��������Ʈ ȣ��
				PlayerUIComponent->OnProgressBarShow.Broadcast();  // ���α׷��� �� ǥ��
				PlayerUIComponent->OnProgressBarTextChanged.Broadcast(SkillName);
			}
		}
	}
}

void URPGPlayerGameplayAbility::HiddenProgressBar(const FGameplayAbilityActorInfo* ActorInfo)
{
	// ActorInfo�κ��� PawnUIComponent�� �����ɴϴ�.
	if (ActorInfo)
	{
		// Pawn UI �������̽��� ĳ�����Ͽ� ���
		if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(ActorInfo->OwnerActor))
		{
			// UI ������Ʈ�� ����
			if (UPlayerUIComponent* PawnUIComponent = PawnUIInterface->GetPlayerUIComponent())
			{
				// ���α׷��� �� UI�� ���̵��� ��������Ʈ ȣ��
				PawnUIComponent->OnProgressBarHidden.Broadcast();  // ���α׷��� �� ǥ��
			}
		}
	}
}

void URPGPlayerGameplayAbility::ShowProcessBarFilling(FString& TimeText, float CurremtTime, float RequireTime)
{
	// AbilityActorInfo�� ����Ͽ� ActorInfo ��������
	if (CurrentActorInfo)
	{
		if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(CurrentActorInfo->OwnerActor))
		{
			UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent();
			if (PlayerUIComponent)
			{
				PlayerUIComponent->OnCurrentProgressChanged.Broadcast(CurremtTime / RequireTime);
				PlayerUIComponent->OnProgressTimeChanged.Broadcast(TimeText);
			}
		}
	}
}

void URPGPlayerGameplayAbility::ProgessCompleted()
{
	if (CurrentActorInfo)
	{
		if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(CurrentActorInfo->OwnerActor))
		{
			UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent();
			if (PlayerUIComponent)
			{
				PlayerUIComponent->OnChargeCompleted.Broadcast();
			}
		}
	}
}