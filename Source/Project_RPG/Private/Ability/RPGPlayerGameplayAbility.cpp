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
#include "RPGFunctionLibrary.h"
#include "Manager/SoundManager.h"
#include "GameInstance/RPGGameInstance.h"
#include "NiagaraFunctionLibrary.h"

#include "RPGGameplayTags.h"
#include "RPGDebugHelper.h"

bool URPGPlayerGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, 
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	// 부모 클래스의 기본 조건 검사
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;  // 부모 클래스에서 조건이 충족되지 않으면 바로 false 반환
	}

	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();

		if (ASC)
		{
			// AbilitySystemComponent에 적용된 모든 태그를 가져오기
			const FGameplayTagContainer& AppliedTags = ASC->GetOwnedGameplayTags();

			// 태그를 디버깅용으로 출력
			for (const FGameplayTag& Tag : AppliedTags)
			{
				UE_LOG(LogTemp, Warning, TEXT("Applied Tag: %s"), *Tag.ToString());
			}
		}
		
		// 폭주 상태 체크: Player.Status.Rage.Activating 태그가 있는지 확인
		if (AbilityTags.HasTag(FGameplayTag::RequestGameplayTag(FName("Player.Ability.IdentitySkill"))))
		{
			// IdentitySkill 태그가 이미 있을 때는 실행 불가
			if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("Player.Status.Rage.Active"))))
			{
				return true;  // 이미 IdentitySkill 태그가 있으면 사용 불가
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
		float CurrentMana = AttributeSet->GetCurrentMana();       // 현재 마나 가져오기
		float ManaCost = RequireManaCost;                    // 마나 소모량 가져오기 (함수로 구현 가능)

		if (ManaCost > 0.0f && CurrentMana < ManaCost)
		{
			UE_LOG(LogTemp, Warning, TEXT("Not enough mana!"));  // 마나 부족 메시지 출력
			return false;  // 마나가 부족하면 활성화 불가
		}
	}

	return true;  // 모든 조건이 충족되면 활성화
}

void URPGPlayerGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!ApplyManaCost(ActorInfo)) // 마나 소모 실패 시
	{
		UE_LOG(LogTemp, Warning, TEXT("Not enough mana to activate ability."));

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true); // 어빌리티 종료
		return;
	}

	if (!CachedAttributeSet)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s] ActivateAbility() - CachedAttributeSet is NULL! Trying to reinitialize..."), *GetName());

		// 강제로 다시 초기화
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
//	// 랜덤 확률로 치명타 여부 판단
//	float RandomChance = FMath::RandRange(0.0f, 1.0f);
//	if (RandomChance <= CriticalChance)
//	{
//		// 치명타 발생 시 데미지 배율 적용
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

	// 기존 태스크 정리 (중복 등록 방지)
	if (WaitGameplayEventTask)
	{
		WaitGameplayEventTask->EndTask();
		WaitGameplayEventTask = nullptr;
	}


	WaitGameplayEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		EventTag,
		nullptr,
		false,  // bOnlyTriggerOnce: 이벤트가 한 번만 트리거될지 여부
		true   // bMatchExact: 태그가 정확히 일치해야 하는지 여부
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

	if (URPGFunctionLibrary::NativeDoesActorHaveTag(CachedTargetActor, RPGGameplayTags::Shared_Status_Invincible))
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

	// Instigator를 어빌리티 소유자로 설정
	if (CurrentActorInfo)
	{
		EventData.Instigator = CurrentActorInfo->OwnerActor.Get();
	}

	EventData.Target = CachedTargetActor;

	// 이벤트 전송(히트 리액션 태그 전송)
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(CachedTargetActor, HitReactTag, EventData);

	GainIdentity();
}

void URPGPlayerGameplayAbility::HandleApplyAOEDamage(const FGameplayEventData& InGameplayEventData)
{
	TArray<FHitResult> HitResults;

	FVector Origin = GetAvatarActorFromActorInfo()->GetActorLocation();
	float Radius = 200.f;  // 예시 반경
	FVector HalfSize = FVector(200.f, 200.f, 100.f);
	FRotator Rotation = FRotator::ZeroRotator;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Pawn));  // Pawn 타입만 체크

	switch (AOETraceType)
	{
	case EAOETraceType::Box:
		HitResults = URPGFunctionLibrary::DoBoxTrace(GetWorld(), Origin, HalfSize, Rotation
			,50.f, ObjectTypes);
		break;
	case EAOETraceType::Sphere:
		HitResults = URPGFunctionLibrary::DoSphereTrace(GetWorld(),
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

	// IdentitySkill 태그가 이미 있을 때는 실행 불가
	if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("Player.Status.Rage.Active"))))
	{
		return;  // 이미 IdentitySkill 태그가 있으면 사용 불가
	}
		
	float IdentityGainAmount = GainIdentityAmount;

	// 게임플레이 이펙트에 수급량 적용
	FGameplayEffectSpecHandle EffectSpecHandle = MakeOutgoingGameplayEffectSpec(GainIdentityEffectClass, 1.0f);

	if (EffectSpecHandle.IsValid())
	{
		FGameplayEffectSpec* EffectSpec = EffectSpecHandle.Data.Get();
		if (EffectSpec)
		{
			// SetByCaller로 수급량 설정
			FGameplayTag IdentityGainTag = 
				FGameplayTag::RequestGameplayTag(FName("Player.Ability.Skill.GainIdentity"));
			EffectSpec->SetSetByCallerMagnitude(IdentityGainTag, IdentityGainAmount);
		}

		// Effect를 적용
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, 
			CurrentActorInfo, CurrentActivationInfo, EffectSpecHandle);

	}
}

//FGameplayEffectSpecHandle URPGPlayerGameplayAbility::MakeCoolDownGameplayEffect()
//{
//	//float CooldownTime = CoolTime;
//
//	if (!CooldownGameplayEffectClass)  // UPROPERTY로 등록해둔 쿨다운 GE 클래스
//	{
//		UE_LOG(LogTemp, Warning, TEXT("CooldownGameplayEffectClass is null!"));
//		return nullptr;
//	}
//
//	// 스펙 생성
//	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CooldownGameplayEffectClass, GetAbilityLevel());
//
//	if (SpecHandle.IsValid())
//	{
//		// SetByCaller 값 지정 (CoolTime은 이 Ability에 있는 float 변수)
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
		float ManaCost = RequireManaCost;  // 어빌리티마다 다른 마나 소모량을 가져올 수 있다고 가정

		if (ManaCost <= 0.0f)  // 마나 소모가 0이거나 음수라면 그냥 통과
		{
			return true;
		}

		if (CurrentMana >= ManaCost)
		{
			AbilitySystemComponent->ApplyModToAttribute(AttributeSet->GetCurrentManaAttribute(),
				EGameplayModOp::Additive, -ManaCost);

			// ActorInfo로부터 PawnUIComponent를 가져옵니다.
			if (ActorInfo)
			{
				// Pawn UI 인터페이스를 캐스팅하여 사용
				if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(ActorInfo->OwnerActor))
				{
					// UI 컴포넌트에 접근
					if (UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent())
					{
						// 프로그레스 바 UI를 보이도록 델리게이트 호출
						PlayerUIComponent->OnCurrentManaChanged.Broadcast
							(AttributeSet->GetCurrentMana()/AttributeSet->GetMaxMana());  // 프로그레스 바 표시
						
						FString ManaText = FString::Printf(TEXT("%.0f/%.0f"),
							AttributeSet->GetCurrentMana(), AttributeSet->GetMaxMana());
						PlayerUIComponent->OnManaTextChanged.Broadcast(ManaText);
					}
				}
			}

			UE_LOG(LogTemp, Log, TEXT("Mana reduced by %f. Current Mana: %f"), ManaCost, CurrentMana - ManaCost);
			return true;  // 마나 소모 성공
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Not enough mana! Current Mana: %f, Required Mana: %f"), CurrentMana, ManaCost);
		}
	}

	return false;  // 마나 소모 실패
}

void URPGPlayerGameplayAbility::ShowProgressBar(const FGameplayAbilityActorInfo* ActorInfo)
{
	if (ActorInfo)
	{
		// Pawn UI 인터페이스를 캐스팅하여 사용
		if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(ActorInfo->OwnerActor))
		{
			// UI 컴포넌트에 접근
			if (UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent())
			{
				// 프로그레스 바 UI를 보이도록 델리게이트 호출
				PlayerUIComponent->OnProgressBarShow.Broadcast();  // 프로그레스 바 표시
				PlayerUIComponent->OnProgressBarTextChanged.Broadcast(SkillName);
			}
		}
	}
}

void URPGPlayerGameplayAbility::HiddenProgressBar(const FGameplayAbilityActorInfo* ActorInfo)
{
	// ActorInfo로부터 PawnUIComponent를 가져옵니다.
	if (ActorInfo)
	{
		// Pawn UI 인터페이스를 캐스팅하여 사용
		if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(ActorInfo->OwnerActor))
		{
			// UI 컴포넌트에 접근
			if (UPlayerUIComponent* PawnUIComponent = PawnUIInterface->GetPlayerUIComponent())
			{
				// 프로그레스 바 UI를 보이도록 델리게이트 호출
				PawnUIComponent->OnProgressBarHidden.Broadcast();  // 프로그레스 바 표시
			}
		}
	}
}

void URPGPlayerGameplayAbility::ShowProcessBarFilling(FString& TimeText, float CurremtTime, float RequireTime)
{
	// AbilityActorInfo를 사용하여 ActorInfo 가져오기
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