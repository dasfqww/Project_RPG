// Fill out your copyright notice in the Description page of Project Settings.

#include "Attribute/RPGAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "RPGFunctionLibrary.h"
#include "RPGGameplayTags.h"
#include "Interface/PawnUIInterface.h"
#include "Component/UI/PawnUIComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Character/RPGBaseCharacter.h"
#include "Character/RPGPlayer.h"
#include "Net/UnrealNetwork.h"

#include "RPGDebugHelper.h"

URPGAttributeSet::URPGAttributeSet()
{
	InitCurrentHealth(1.f);
	InitMaxHealth(1.f);
	InitCurrentMana(1.f);
	InitMaxMana(1.f);
	InitCurrentRage(1.f);
	InitMaxRage(1.f);
	InitCurrentIdentityGauge(0.f);
	InitMaxIdentityGauge(100.f);
	InitIdentityGainMultiplier(1.f);
	InitAttack(1.f);
	InitDefense(1.f);
	InitCriticalChance(0.3f);
	InitCriticalDamage(2.f);
}

void URPGAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, AttackSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, CurrentHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, CurrentMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, CurrentRage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, MaxRage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, CurrentIdentityGauge, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, MaxIdentityGauge, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, IdentityGainMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, Attack, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, TakeDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, CriticalChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGAttributeSet, CriticalDamage, COND_None, REPNOTIFY_Always);
}

void URPGAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	if (!CachedPawnUIInterface.IsValid())
	{
		CachedPawnUIInterface=TWeakInterfacePtr<IPawnUIInterface>(Data.Target.GetAvatarActor());
	}

	checkf(CachedPawnUIInterface.IsValid(), TEXT("%s didn't implement IPawnUIInterface"),
		*Data.Target.GetAvatarActor()->GetActorNameOrLabel());

	UPawnUIComponent* PawnUIComponent = CachedPawnUIInterface->GetPawnUIComponent();

	checkf(PawnUIComponent, TEXT("Couldn't extract a PawnUIComponent from %s"), 
		*Data.Target.GetAvatarActor()->GetActorNameOrLabel());

	if (Data.EvaluatedData.Attribute == GetCurrentHealthAttribute())
	{
		const float NewCurrentHealth = FMath::Clamp(GetCurrentHealth(), 0.f, GetMaxHealth());

		SetCurrentHealth(NewCurrentHealth);

		PawnUIComponent->OnCurrentHealthChanged.Broadcast(GetCurrentHealth()/GetMaxHealth());

		FString HealthText = FString::Printf(TEXT("%.0f/%.0f"), NewCurrentHealth, GetMaxHealth());

		PawnUIComponent->OnHealthTextChangedDelegate.Broadcast(HealthText);
	}

	if (Data.EvaluatedData.Attribute == GetCurrentManaAttribute())
	{
		const float NewCurrentMana = FMath::Clamp(GetCurrentMana(), 0.f, GetMaxMana());

		SetCurrentMana(NewCurrentMana);

		PawnUIComponent->OnCurrentManaChanged.Broadcast(GetCurrentMana() / GetMaxMana());

		FString ManaText = FString::Printf(TEXT("%.0f/%.0f"), NewCurrentMana, GetMaxMana());
		PawnUIComponent->OnManaTextChanged.Broadcast(ManaText);
	}

	if (Data.EvaluatedData.Attribute==GetCurrentRageAttribute())
	{
		const float NewCurrentRage = FMath::Clamp(GetCurrentRage(), 0.f, GetMaxRage());

		SetCurrentRage(NewCurrentRage);

		if (GetCurrentRage() == GetMaxRage())
		{
			URPGFunctionLibrary::AddGameplayTagToActorIfNone
				(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Rage_Full);
		}
		else if (GetCurrentRage() == 0.f)
		{
			URPGFunctionLibrary::AddGameplayTagToActorIfNone
				(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Rage_None);
		}
		else
		{
			URPGFunctionLibrary::RemoveGameplayTagFromActorIfFound
				(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Rage_Full);
			URPGFunctionLibrary::RemoveGameplayTagFromActorIfFound
				(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Rage_None);
		}

		if (UPlayerUIComponent* PlayerUIComponent = CachedPawnUIInterface->GetPlayerUIComponent())
		{
			PlayerUIComponent->OnCurrentRageChanged.Broadcast(GetCurrentRage()/GetMaxRage());
		}
	}

	if (Data.EvaluatedData.Attribute == GetCurrentIdentityGaugeAttribute())
	{
		const float NewCurrentIdentity = FMath::Clamp(GetCurrentIdentityGauge(), 0.f, GetMaxIdentityGauge());
		SetCurrentIdentityGauge(NewCurrentIdentity);

		// 게이지 상태에 따른 태그 관리
		if (GetCurrentIdentityGauge() == GetMaxIdentityGauge())
		{
			URPGFunctionLibrary::AddGameplayTagToActorIfNone(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Identity_Full);
		}
		else if (GetCurrentIdentityGauge() == 0.f)
		{
			URPGFunctionLibrary::AddGameplayTagToActorIfNone(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Identity_None);
		}
		else
		{
			URPGFunctionLibrary::RemoveGameplayTagFromActorIfFound(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Identity_Full);
			URPGFunctionLibrary::RemoveGameplayTagFromActorIfFound(Data.Target.GetAvatarActor(), RPGGameplayTags::Player_Status_Identity_None);
		}

		// UI 업데이트
		if (UPlayerUIComponent* PlayerUIComponent = CachedPawnUIInterface->GetPlayerUIComponent())
		{
			PlayerUIComponent->OnCurrentIdentityGaugeChanged.Broadcast(GetCurrentIdentityGauge() / GetMaxIdentityGauge());
		}
	}

	if (Data.EvaluatedData.Attribute==GetTakeDamageAttribute())
	{
		const float prevHealth = GetCurrentHealth();
		const int32 takeDamage = GetTakeDamage();

		//const float criticalChance = GetCriticalChance();
		////const float criticalDamageMultiplier = GetCriticalDamage();

		//bool IsCritical = false;

		//float RandomChance = FMath::RandRange(0.0f, 1.0f);
		//if (RandomChance <= criticalChance)
		//{
		//	IsCritical = true;
		//	// ġŸ ߻    
		//	//takeDamage *= criticalDamageMultiplier;
		//}
		//		
		////const int32 FinalTakeDamage = takeDamage;

		//ARPGBaseCharacter* TargetCharacter = Cast<ARPGBaseCharacter>(GetOwningActor());
		//if (TargetCharacter&&TargetCharacter->IsPlayerControlled())
		//{
		//	FVector Location;
		//	ARPGPlayer* Player = Cast<ARPGPlayer>(TargetCharacter);
		//	if (Player)
		//	{
		//		Location = Player->GetDamageFontComponent()->GetComponentLocation();
		//	}

		//	TargetCharacter->ShowDamageFont(takeDamage, Location, IsCritical, true);
		//	//Debug::Print("Player take damage..");
		//}

		//else
		//{
		//	TargetCharacter->ShowDamageFont(takeDamage, GetOwningActor()->GetActorLocation(), IsCritical, false);
		//	//Debug::Print("NPC take damage..");
		//}

		const float NewCurrentHealth = FMath::Clamp(prevHealth- takeDamage, 0.f, GetMaxHealth());

		SetCurrentHealth(NewCurrentHealth);
	
		/*const FString DebugString = FString::Printf(
			TEXT("prev Health: %f, TakeDamage: %d, NewCurrentHealth: %f"),
			prevHealth,
			FMath::RoundToInt32(FinalTakeDamage),
			NewCurrentHealth
		);

		Debug::Print(DebugString, FColor::Green);*/

		// ü UI Ʈ
		PawnUIComponent->OnCurrentHealthChanged.Broadcast(NewCurrentHealth / GetMaxHealth());

		//ݿø  Ȯغ.(ϴ ̴ )
		if (AActor* Owner = GetOwningActor())
		{
			if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(Owner))
			{
				if (UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent())
				{				
					FString HealthText = FString::Printf(TEXT("%.0f/%.0f"), NewCurrentHealth, GetMaxHealth());
					PlayerUIComponent->OnHealthTextChangedDelegate.Broadcast(HealthText);
				}
			}
		}


		//TODO: notify the ui
		if (GetCurrentHealth() == 0.f)
		{
			URPGFunctionLibrary::AddGameplayTagToActorIfNone(
				Data.Target.GetAvatarActor(), 
				RPGGameplayTags::Shared_Status_Death
			);
			
		}
	}
}

void URPGAttributeSet::OnRep_AttackSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, AttackSpeed, OldValue);
}

void URPGAttributeSet::OnRep_MoveSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, MoveSpeed, OldValue);
}

void URPGAttributeSet::OnRep_CurrentHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, CurrentHealth, OldValue);
}

void URPGAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, MaxHealth, OldValue);
}

void URPGAttributeSet::OnRep_CurrentMana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, CurrentMana, OldValue);
}

void URPGAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, MaxMana, OldValue);
}

void URPGAttributeSet::OnRep_CurrentRage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, CurrentRage, OldValue);
}

void URPGAttributeSet::OnRep_MaxRage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, MaxRage, OldValue);
}

void URPGAttributeSet::OnRep_CurrentIdentityGauge(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, CurrentIdentityGauge, OldValue);
}

void URPGAttributeSet::OnRep_MaxIdentityGauge(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, MaxIdentityGauge, OldValue);
}

void URPGAttributeSet::OnRep_IdentityGainMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, IdentityGainMultiplier, OldValue);
}

void URPGAttributeSet::OnRep_Attack(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, Attack, OldValue);
}

void URPGAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, Defense, OldValue);
}

void URPGAttributeSet::OnRep_TakeDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, TakeDamage, OldValue);
}

void URPGAttributeSet::OnRep_CriticalChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, CriticalChance, OldValue);
}

void URPGAttributeSet::OnRep_CriticalDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, CriticalDamage, OldValue);
}