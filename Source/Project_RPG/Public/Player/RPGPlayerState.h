// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGAbilitySet.h"
#include "DataAsset/Definition/RPGGladiatorData.h"
#include "Economy/RPGCurrencyTypes.h"
#include "ModularPlayerState.h"
#include "RPGPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGGladiatorClassChanged,
	ERPGGladiatorCharacterClass,
	SelectedClass);
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

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Class")
	void ServerSelectClass(ERPGGladiatorCharacterClass NewClass);

	UFUNCTION(BlueprintPure, Category = "RPG|Class")
	ERPGGladiatorCharacterClass GetSelectedClass() const { return SelectedClass; }

	void SetAuthenticatedIdentity(
		const FString& InCharacterId,
		const FString& InSteamId,
		const FString& InDungeonSessionId);

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

	UPROPERTY(BlueprintAssignable, Category = "RPG|Class")
	FRPGGladiatorClassChanged OnSelectedClassChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online")
	FRPGCharacterIdentityChanged OnCharacterIdentityChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online")
	FRPGDungeonSessionIdentityChanged OnDungeonSessionIdentityChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Economy")
	FRPGCurrencyWalletChanged OnCurrencyWalletChanged;

protected:
	UFUNCTION()
	void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);

	UFUNCTION()
	void OnRep_SelectedClass();

	UFUNCTION()
	void OnRep_BackendCharacterId();

	UFUNCTION()
	void OnRep_BackendDungeonSessionId();

	UFUNCTION()
	void OnRep_CurrencyWallet();

	void HandleCurrencyTransactionCommitted(
		const FRPGEconomyCommitResult& Result);

	UPROPERTY(ReplicatedUsing = OnRep_SelectedClass, BlueprintReadOnly, Category = "RPG|Class")
	ERPGGladiatorCharacterClass SelectedClass = ERPGGladiatorCharacterClass::Count;

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
	UPROPERTY(Transient)
	FRPGAbilitySet_GrantedHandles ClassAbilitySetHandles;

	uint64 CurrencyWalletRequestGeneration = 0;
	FTimerHandle CurrencyWalletRefreshTimerHandle;
};
