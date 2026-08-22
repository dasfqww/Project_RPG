// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/RPGPlayerState.h"

#include "Character/RPGBaseCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Economy/Backend/RPGEconomyBackendSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	constexpr float CurrencyWalletRefreshIntervalSeconds = 15.0f;
}

void ARPGPlayerState::BeginPlay()
{
	Super::BeginPlay();
	OnPawnSet.AddUniqueDynamic(this, &ThisClass::HandlePawnSet);
	if (HasAuthority())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (URPGEconomyBackendSubsystem* EconomySubsystem =
				GameInstance->GetSubsystem<URPGEconomyBackendSubsystem>())
			{
				EconomySubsystem->OnTransactionCommitted().AddUObject(
					this,
					&ThisClass::HandleCurrencyTransactionCommitted);
			}
		}
	}
}

void ARPGPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	++CurrencyWalletRequestGeneration;
	GetWorldTimerManager().ClearTimer(CurrencyWalletRefreshTimerHandle);
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (URPGEconomyBackendSubsystem* EconomySubsystem =
			GameInstance->GetSubsystem<URPGEconomyBackendSubsystem>())
		{
			EconomySubsystem->OnTransactionCommitted().RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ARPGPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGPlayerState, SelectedClass);
	DOREPLIFETIME_CONDITION(
		ARPGPlayerState,
		BackendCharacterId,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		ARPGPlayerState,
		AuthenticatedSteamId,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		ARPGPlayerState,
		BackendDungeonSessionId,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		ARPGPlayerState,
		BackendRosterId,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		ARPGPlayerState,
		CurrencyBalances,
		COND_OwnerOnly);
}

void ARPGPlayerState::SetAuthenticatedIdentity(
	const FString& InCharacterId,
	const FString& InSteamId,
	const FString& InDungeonSessionId)
{
	if (!HasAuthority()
		|| InCharacterId.IsEmpty()
		|| InSteamId.IsEmpty()
		|| InDungeonSessionId.IsEmpty())
	{
		return;
	}

	AuthenticatedSteamId = InSteamId;
	BackendDungeonSessionId = InDungeonSessionId;
	BackendCharacterId = InCharacterId;
	OnCharacterIdentityChanged.Broadcast(BackendCharacterId);
	OnDungeonSessionIdentityChanged.Broadcast(BackendDungeonSessionId);
	ForceNetUpdate();
	RefreshCurrencyWallet();
	GetWorldTimerManager().SetTimer(
		CurrencyWalletRefreshTimerHandle,
		this,
		&ThisClass::RefreshCurrencyWallet,
		CurrencyWalletRefreshIntervalSeconds,
		true,
		CurrencyWalletRefreshIntervalSeconds);
}

int64 ARPGPlayerState::GetCurrencyBalance(const FName CurrencyCode) const
{
	const FRPGCurrencyBalance* Found = CurrencyBalances.FindByPredicate(
		[CurrencyCode](const FRPGCurrencyBalance& Balance)
		{
			return Balance.CurrencyCode == CurrencyCode;
		});
	return Found ? Found->Balance : 0;
}

void ARPGPlayerState::RefreshCurrencyWallet()
{
	if (!HasAuthority() || BackendCharacterId.IsEmpty())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	URPGEconomyBackendSubsystem* EconomySubsystem = GameInstance
		? GameInstance->GetSubsystem<URPGEconomyBackendSubsystem>()
		: nullptr;
	if (!EconomySubsystem)
	{
		return;
	}

	const FString RequestedCharacterId = BackendCharacterId;
	const uint64 RequestGeneration = ++CurrencyWalletRequestGeneration;
	TWeakObjectPtr<ARPGPlayerState> WeakThis(this);
	EconomySubsystem->LoadWallet(
		RequestedCharacterId,
		[WeakThis, RequestedCharacterId, RequestGeneration](
			FRPGEconomyWalletResult Result)
		{
			if (!WeakThis.IsValid() ||
				!WeakThis->HasAuthority() ||
				WeakThis->BackendCharacterId != RequestedCharacterId ||
				WeakThis->CurrencyWalletRequestGeneration != RequestGeneration)
			{
				return;
			}

			if (!Result.WasSuccessful())
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("Failed to load the currency wallet for %s: %s"),
					*RequestedCharacterId,
					*Result.Error);
				return;
			}

			if (Result.AccountId != WeakThis->AuthenticatedSteamId)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("Rejected an economy wallet with a mismatched account."));
				return;
			}

			WeakThis->BackendRosterId = MoveTemp(Result.RosterId);
			WeakThis->CurrencyBalances = MoveTemp(Result.Balances);
			WeakThis->OnCurrencyWalletChanged.Broadcast();
			WeakThis->ForceNetUpdate();
		});
}

void ARPGPlayerState::HandleCurrencyTransactionCommitted(
	const FRPGEconomyCommitResult& Result)
{
	if (!HasAuthority() || !Result.WasSuccessful())
	{
		return;
	}

	const bool bAffectsWallet = Result.Changes.ContainsByPredicate(
		[this](const FRPGCurrencyChangeResult& Change)
		{
			switch (Change.Scope)
			{
			case ERPGCurrencyScope::Account:
				return Change.OwnerId == AuthenticatedSteamId;
			case ERPGCurrencyScope::Roster:
				return Change.OwnerId == BackendRosterId;
			case ERPGCurrencyScope::Character:
				return Change.OwnerId == BackendCharacterId;
			default:
				return false;
			}
		});
	if (bAffectsWallet)
	{
		RefreshCurrencyWallet();
	}
}

void ARPGPlayerState::ServerSelectClass_Implementation(const ERPGGladiatorCharacterClass NewClass)
{
	if (!HasAuthority() || NewClass == ERPGGladiatorCharacterClass::Count || NewClass == SelectedClass)
	{
		return;
	}

	const URPGClassData* ClassData = URPGClassData::GetDefaultClassData();
	ARPGBaseCharacter* Character = GetPawn<ARPGBaseCharacter>();
	URPGAbilitySystemComponent* AbilitySystemComponent = Character
		? Character->GetRPGAbilitySystemComponent()
		: nullptr;
	if (!ClassData || !AbilitySystemComponent || !ClassData->FindClassInfo(NewClass))
	{
		return;
	}

	ClassAbilitySetHandles.TakeFromAbilitySystem(AbilitySystemComponent);
	if (!ClassData->GiveClassAbilitiesToAbilitySystem(
		NewClass,
		AbilitySystemComponent,
		&ClassAbilitySetHandles,
		this))
	{
		return;
	}

	SelectedClass = NewClass;
	OnSelectedClassChanged.Broadcast(SelectedClass);
	ForceNetUpdate();
}

void ARPGPlayerState::OnRep_SelectedClass()
{
	OnSelectedClassChanged.Broadcast(SelectedClass);
}

void ARPGPlayerState::OnRep_BackendCharacterId()
{
	OnCharacterIdentityChanged.Broadcast(BackendCharacterId);
}

void ARPGPlayerState::OnRep_BackendDungeonSessionId()
{
	OnDungeonSessionIdentityChanged.Broadcast(BackendDungeonSessionId);
}

void ARPGPlayerState::OnRep_CurrencyWallet()
{
	OnCurrencyWalletChanged.Broadcast();
}

void ARPGPlayerState::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	if (!HasAuthority() || NewPawn == OldPawn)
	{
		return;
	}

	if (const ARPGBaseCharacter* OldCharacter = Cast<ARPGBaseCharacter>(OldPawn))
	{
		ClassAbilitySetHandles.TakeFromAbilitySystem(OldCharacter->GetRPGAbilitySystemComponent());
	}

	if (SelectedClass == ERPGGladiatorCharacterClass::Count)
	{
		return;
	}

	const URPGClassData* ClassData = URPGClassData::GetDefaultClassData();
	ARPGBaseCharacter* NewCharacter = Cast<ARPGBaseCharacter>(NewPawn);
	if (ClassData && NewCharacter)
	{
		ClassData->GiveClassAbilitiesToAbilitySystem(
			SelectedClass,
			NewCharacter->GetRPGAbilitySystemComponent(),
			&ClassAbilitySetHandles,
			this);
	}
}

