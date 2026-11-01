// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/RPGPlayerState.h"

#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Economy/Backend/RPGEconomyBackendSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Security/RPGSecurityTelemetrySubsystem.h"
#include "TimerManager.h"

namespace
{
	constexpr float CurrencyWalletRefreshIntervalSeconds = 15.0f;
}

void ARPGPlayerState::BeginPlay()
{
	Super::BeginPlay();
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
	bAuthenticatedIdentityRevoked = true;
	BackendConnectionGeneration = 0;
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
	DOREPLIFETIME_CONDITION(ARPGPlayerState, RewardDeliveryNotice, COND_OwnerOnly);
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
	const FString& InDungeonSessionId,
	int64 InConnectionGeneration)
{
	if (!HasAuthority()
		|| bAuthenticatedIdentityRevoked
		|| InCharacterId.IsEmpty()
		|| InSteamId.IsEmpty()
		|| InDungeonSessionId.IsEmpty())
	{
		return;
	}

	++CurrencyWalletRequestGeneration;
	if (BackendCharacterId != InCharacterId || BackendDungeonSessionId != InDungeonSessionId
		|| AuthenticatedSteamId != InSteamId)
	{
		RewardDeliveryNotice = FRPGDungeonRewardDeliveryNotice();
		OnRep_RewardDeliveryNotice();
		if (bAuthenticatedIdentityRevoked) return;
	}
	AuthenticatedSteamId = InSteamId;
	BackendDungeonSessionId = InDungeonSessionId;
	BackendCharacterId = InCharacterId;
	BackendConnectionGeneration = InConnectionGeneration > 0 ? InConnectionGeneration : 0;
	OnCharacterIdentityChanged.Broadcast(BackendCharacterId);
	if (bAuthenticatedIdentityRevoked) return;
	OnDungeonSessionIdentityChanged.Broadcast(BackendDungeonSessionId);
	if (bAuthenticatedIdentityRevoked) return;
	ForceNetUpdate();
	RefreshCurrencyWallet();
	if (bAuthenticatedIdentityRevoked) return;
	GetWorldTimerManager().SetTimer(
		CurrencyWalletRefreshTimerHandle,
		this,
		&ThisClass::RefreshCurrencyWallet,
		CurrencyWalletRefreshIntervalSeconds,
		true,
		CurrencyWalletRefreshIntervalSeconds);
}

bool ARPGPlayerState::RevokeAuthenticatedIdentity(FString ExpectedSessionId,
	FString ExpectedCharacterId, FString ExpectedSteamId)
{
	if (!HasAuthority() || !HasAuthenticatedCharacter()
		|| BackendDungeonSessionId != ExpectedSessionId
		|| BackendCharacterId != ExpectedCharacterId || AuthenticatedSteamId != ExpectedSteamId)
	{
		return false;
	}

	// Raise the irreversible barrier before any notification can re-enter us.
	bAuthenticatedIdentityRevoked = true;
	BackendConnectionGeneration = 0;
	++CurrencyWalletRequestGeneration;
	GetWorldTimerManager().ClearTimer(CurrencyWalletRefreshTimerHandle);
	BackendCharacterId.Reset();
	AuthenticatedSteamId.Reset();
	BackendDungeonSessionId.Reset();
	BackendRosterId.Reset();
	CurrencyBalances.Reset();
	RewardDeliveryNotice = FRPGDungeonRewardDeliveryNotice();
	if (APlayerController* Controller = GetPlayerController();
		Controller && Controller->GetPlayerState<ARPGPlayerState>() == this)
	{
		if (APawn* Pawn = Controller->GetPawn(); Pawn && Pawn->GetPlayerState<ARPGPlayerState>() == this)
		{
			if (auto* ASC = Pawn->FindComponentByClass<URPGAbilitySystemComponent>())
				ASC->RevokeDungeonGameplayAuthority(*this);
		}
		if (auto* Commands = Controller->FindComponentByClass<URPGItemCommandComponent>())
			Commands->InvalidateForRevokedIdentity(*this);
		if (auto* Projection = Controller->FindComponentByClass<URPGInventoryProjectionComponent>())
			Projection->InvalidateForRevokedIdentity(*this);
	}
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (auto* Telemetry = Instance->GetSubsystem<URPGSecurityTelemetrySubsystem>())
			Telemetry->QuarantineRevokedDungeonMember(ExpectedSessionId, ExpectedCharacterId, ExpectedSteamId);
	}
	OnCharacterIdentityChanged.Broadcast(BackendCharacterId);
	OnDungeonSessionIdentityChanged.Broadcast(BackendDungeonSessionId);
	OnCurrencyWalletChanged.Broadcast();
	OnRep_RewardDeliveryNotice();
	ForceNetUpdate();
	return true;
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
	if (!HasAuthority() || !HasAuthenticatedCharacter())
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
	const FString RequestedSteamId = AuthenticatedSteamId;
	const FString RequestedSessionId = BackendDungeonSessionId;
	const uint64 RequestGeneration = ++CurrencyWalletRequestGeneration;
	TWeakObjectPtr<ARPGPlayerState> WeakThis(this);
	EconomySubsystem->LoadWallet(
		RequestedCharacterId,
		[WeakThis, RequestedCharacterId, RequestedSteamId, RequestedSessionId, RequestGeneration](
			FRPGEconomyWalletResult Result)
		{
			if (WeakThis.IsValid())
				WeakThis->HandleCurrencyWalletLoaded(MoveTemp(Result), RequestedCharacterId,
					RequestedSteamId, RequestedSessionId, RequestGeneration);
		});
}

void ARPGPlayerState::HandleCurrencyWalletLoaded(FRPGEconomyWalletResult Result,
	const FString& RequestedCharacterId, const FString& RequestedSteamId,
	const FString& RequestedSessionId, const uint64 RequestGeneration)
{
	if (!HasAuthority() || !HasAuthenticatedCharacter()
		|| BackendCharacterId != RequestedCharacterId || AuthenticatedSteamId != RequestedSteamId
		|| BackendDungeonSessionId != RequestedSessionId || CurrencyWalletRequestGeneration != RequestGeneration)
		return;
	if (!Result.WasSuccessful())
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load the currency wallet for %s: %s"),
			*RequestedCharacterId, *Result.Error);
		return;
	}
	if (Result.AccountId != RequestedSteamId || Result.CharacterId != RequestedCharacterId)
	{
		UE_LOG(LogTemp, Error, TEXT("Rejected an economy wallet with a mismatched identity."));
		return;
	}
	BackendRosterId = MoveTemp(Result.RosterId);
	CurrencyBalances = MoveTemp(Result.Balances);
	OnCurrencyWalletChanged.Broadcast();
	ForceNetUpdate();
}

bool ARPGPlayerState::CanApplyCompletedRewardWallet(const FRPGEconomyWalletResult& Wallet, const FString& SessionId) const
{
	if (!HasAuthority() || !HasAuthenticatedCharacter() || !Wallet.WasSuccessful()
		|| BackendDungeonSessionId != SessionId || BackendCharacterId != Wallet.CharacterId
		|| AuthenticatedSteamId != Wallet.AccountId || Wallet.RosterId.IsEmpty()
		|| (!BackendRosterId.IsEmpty() && BackendRosterId != Wallet.RosterId)) return false;
	for (const FRPGCurrencyBalance& Balance : Wallet.Balances)
	{
		const FString& ExpectedOwner = Balance.Scope == ERPGCurrencyScope::Account ? Wallet.AccountId
			: (Balance.Scope == ERPGCurrencyScope::Roster ? Wallet.RosterId : Wallet.CharacterId);
		if (Balance.OwnerId != ExpectedOwner) return false;
	}
	return true;
}

bool ARPGPlayerState::ApplyCompletedRewardWallet(const FRPGEconomyWalletResult& Wallet, const FRPGDungeonRewardDeliveryNotice& Notice)
{
	if (!CanApplyCompletedRewardWallet(Wallet, Notice.DungeonSessionId) || !Notice.bApplied
		|| Notice.InventoryQuantity < 0 || Notice.MailQuantity < 0) return false;
	++CurrencyWalletRequestGeneration;
	GetWorldTimerManager().ClearTimer(CurrencyWalletRefreshTimerHandle);
	BackendRosterId = Wallet.RosterId;
	CurrencyBalances = Wallet.Balances;
	RewardDeliveryNotice = Notice;
	OnCurrencyWalletChanged.Broadcast();
	OnRep_RewardDeliveryNotice();
	ForceNetUpdate();
	return true;
}

void ARPGPlayerState::OnRep_RewardDeliveryNotice()
{
	OnDungeonRewardDelivered.Broadcast(RewardDeliveryNotice);
}

void ARPGPlayerState::HandleCurrencyTransactionCommitted(
	const FRPGEconomyCommitResult& Result)
{
	if (!HasAuthority() || !HasAuthenticatedCharacter() || !Result.WasSuccessful())
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

