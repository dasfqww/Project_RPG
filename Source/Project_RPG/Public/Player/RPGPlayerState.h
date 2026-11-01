// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "ModularPlayerState.h"
#include "RPGPlayerState.generated.h"

struct FRPGEconomyWalletResult;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGDungeonRewardDelivered,
	const FRPGDungeonRewardDeliveryNotice&, Notice);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGCharacterIdentityChanged,
	const FString&,
	CharacterId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGDungeonSessionIdentityChanged,
	const FString&,
	DungeonSessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGCurrencyWalletChanged);

struct FRPGEconomyCommitResult;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGPlayerState : public AModularPlayerState
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetAuthenticatedIdentity(
		const FString& InCharacterId,
		const FString& InSteamId,
		const FString& InDungeonSessionId,
		int64 InConnectionGeneration = 0);
	/** Server-only consumed-ticket generation; not proof of admission confirmation. */
	int64 GetBackendConnectionGeneration() const { return BackendConnectionGeneration; }

	/** Native server boundary; caller must first verify an immutable opt-out receipt.
	 * The exact identity is revoked once. Re-admission requires a new PlayerState.
	 * This does not itself cancel combat or eject the connection. */
	bool RevokeAuthenticatedIdentity(FString ExpectedSessionId,
		FString ExpectedCharacterId, FString ExpectedSteamId);
	bool IsAuthenticatedIdentityRevoked() const { return bAuthenticatedIdentityRevoked; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online")
	const FString& GetBackendCharacterId() const { return BackendCharacterId; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online")
	const FString& GetAuthenticatedSteamId() const { return AuthenticatedSteamId; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online")
	const FString& GetBackendDungeonSessionId() const
	{
		return BackendDungeonSessionId;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Online")
	bool HasAuthenticatedCharacter() const
	{
		return !BackendCharacterId.IsEmpty()
			&& !bAuthenticatedIdentityRevoked
			&& !AuthenticatedSteamId.IsEmpty()
			&& !BackendDungeonSessionId.IsEmpty();
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Economy")
	const FString& GetBackendRosterId() const { return BackendRosterId; }

	UFUNCTION(BlueprintPure, Category = "RPG|Economy")
	TArray<FRPGCurrencyBalance> GetCurrencyBalances() const
	{
		return CurrencyBalances;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Economy")
	int64 GetCurrencyBalance(FName CurrencyCode) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Economy")
	void RefreshCurrencyWallet();

	bool CanApplyCompletedRewardWallet(const FRPGEconomyWalletResult& Wallet, const FString& SessionId) const;
	bool ApplyCompletedRewardWallet(const FRPGEconomyWalletResult& Wallet, const FRPGDungeonRewardDeliveryNotice& Notice);
	UFUNCTION(BlueprintPure, Category = "RPG|Reward")
	FRPGDungeonRewardDeliveryNotice GetRewardDeliveryNotice() const { return RewardDeliveryNotice; }
	UPROPERTY(BlueprintAssignable, Category = "RPG|Reward")
	FRPGDungeonRewardDelivered OnDungeonRewardDelivered;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online")
	FRPGCharacterIdentityChanged OnCharacterIdentityChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online")
	FRPGDungeonSessionIdentityChanged OnDungeonSessionIdentityChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Economy")
	FRPGCurrencyWalletChanged OnCurrencyWalletChanged;

protected:
	UFUNCTION()
	void OnRep_BackendCharacterId();

	UFUNCTION()
	void OnRep_BackendDungeonSessionId();

	UFUNCTION()
	void OnRep_CurrencyWallet();
	UFUNCTION()
	void OnRep_RewardDeliveryNotice();
	UPROPERTY(ReplicatedUsing = OnRep_RewardDeliveryNotice)
	FRPGDungeonRewardDeliveryNotice RewardDeliveryNotice;

	void HandleCurrencyTransactionCommitted(
		const FRPGEconomyCommitResult& Result);

	/** Backend-owned character GUID, assigned only after join-ticket validation. */
	UPROPERTY(
		ReplicatedUsing = OnRep_BackendCharacterId,
		BlueprintReadOnly,
		Category = "RPG|Online")
	FString BackendCharacterId;

	/** SteamID authenticated by the backend and matched to the connection. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "RPG|Online")
	FString AuthenticatedSteamId;

	/** Backend-owned dungeon session GUID validated from the consumed ticket. */
	UPROPERTY(
		ReplicatedUsing = OnRep_BackendDungeonSessionId,
		BlueprintReadOnly,
		Category = "RPG|Online")
	FString BackendDungeonSessionId;

	/** Roster GUID resolved by the backend for the authenticated character. */
	UPROPERTY(
		ReplicatedUsing = OnRep_CurrencyWallet,
		BlueprintReadOnly,
		Category = "RPG|Economy")
	FString BackendRosterId;

	/** Server-owned wallet projection; only the owning client receives it. */
	UPROPERTY(
		ReplicatedUsing = OnRep_CurrencyWallet,
		BlueprintReadOnly,
		Category = "RPG|Economy")
	TArray<FRPGCurrencyBalance> CurrencyBalances;

private:
	friend struct FRPGDungeonAuthorityRevocationTestAccess;
	void HandleCurrencyWalletLoaded(FRPGEconomyWalletResult Result,
		const FString& RequestedCharacterId, const FString& RequestedSteamId,
		const FString& RequestedSessionId, uint64 RequestGeneration);
	bool bAuthenticatedIdentityRevoked = false;
	int64 BackendConnectionGeneration = 0;

	uint64 CurrencyWalletRequestGeneration = 0;
	FTimerHandle CurrencyWalletRefreshTimerHandle;
};
