// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "GameFramework/GameModeBase.h"
#include "Manager/HttpWebManager.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameModeBase.generated.h"

class URPGItemBase;
class ARPGPlayer;
class URPGContentClearPanel;
class URPGDungeonRewardDefinition;

UENUM(BlueprintType)
enum class EGameModeType : uint8
{
	Village UMETA(DisplayName = "Village"),
	Content UMETA(DisplayName = "Content")
};

/**
 * 
 */
UCLASS(Config = Game)
class PROJECT_RPG_API ARPGGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARPGGameModeBase();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreLoginAsync(
		const FString& Options,
		const FString& Address,
		const FUniqueNetIdRepl& UniqueId,
		const FOnPreLoginCompleteDelegate& OnComplete) override;
	virtual FString InitNewPlayer(
		APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId,
		const FString& Options,
		const FString& Portal = TEXT("")) override;

	UFUNCTION(BlueprintCallable)
	void SetGameDifficulty(ERPGGameDifficulty InGameDifficulty);

	/**
	 * Reports the authoritative dungeon result and stops lease heartbeats.
	 * Call this only after reward/persistence work has completed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Dungeon")
	void ReportDungeonFinished(bool bCleared);

	/**
	 * Queues the same currency reward for every backend session member. The
	 * backend persists and atomically settles the party before marking Cleared.
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Dungeon")
	void ReportDungeonClearedWithCurrencyReward(
		const FString& RewardVersion,
		const TArray<FRPGCurrencyChange>& CurrencyChanges);

	/** Queues currency and item grants in one backend reward transaction. */
	UFUNCTION(BlueprintCallable, Category = "Online|Dungeon")
	void ReportDungeonClearedWithRewards(
		const FString& RewardVersion,
		const TArray<FRPGCurrencyChange>& CurrencyChanges,
		const TArray<FRPGDungeonItemReward>& ItemRewards);

	/**
	 * Settles the reward definition assigned to the current difficulty. This is
	 * the preferred single call for an authoritative dungeon-clear event.
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Dungeon")
	void ReportConfiguredDungeonClear();

protected:
	UFUNCTION()
	void HandleJoinTicketConsumed(
		const FString& JoinTicket,
		const FString& DungeonSessionId,
		const FString& CharacterId,
		const FString& SteamId,
		bool bSuccess);

	UFUNCTION()
	void HandleDungeonSessionUpdated(
		const FRPGDungeonSession& DungeonSession,
		bool bSuccess);

	UFUNCTION()
	void HandleDungeonRewardSettlementRequested(
		const FString& State,
		int32 HttpStatus,
		bool bSuccess);

	void SendDungeonSessionHeartbeat();
	void SendPendingDungeonRewardSettlement();

	/** Enforced only by a true Dedicated Server process. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Online|Admission")
	bool bRequireBackendJoinTicket = true;

	/** Rejects a valid ticket if it belongs to a different Steam connection. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Online|Admission")
	bool bRequireSteamIdentityMatch = true;

	/** Must remain comfortably below the backend's active lease duration. */
	UPROPERTY(
		Config,
		EditDefaultsOnly,
		Category = "Online|Dungeon",
		meta = (ClampMin = "5.0"))
	float DungeonHeartbeatIntervalSeconds = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Game Settings")
	ERPGGameDifficulty CurrentGameDifficulty;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reward")
	TObjectPtr<UDataTable> RewardDataTable;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reward")
	TObjectPtr<UDataTable> ItemDataTable;

	/** Versioned backend rewards selected by CurrentGameDifficulty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reward")
	TMap<ERPGGameDifficulty, TObjectPtr<URPGDungeonRewardDefinition>>
		DungeonClearRewardsByDifficulty;

	UPROPERTY()
	TArray<TObjectPtr<URPGItemBase>> RewardItems;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reward")
	TSubclassOf<URPGContentClearPanel> ClearPanelClass;

	UPROPERTY(EditDefaultsOnly, Category = "Clear Panel UI Transform")
	float CoordX;

	UPROPERTY(EditDefaultsOnly, Category = "Reward")
	bool bGiveReward;

	UFUNCTION(BlueprintCallable)
	void GiveContentReward(ARPGPlayer* Player);

	void InitializeRewardItems(FName InName);

public:
	FORCEINLINE ERPGGameDifficulty GetCurrentGameDifficulty() const { return CurrentGameDifficulty; }
	FORCEINLINE const TArray<TObjectPtr<URPGItemBase>>& GetRewardItems() const { return RewardItems; }

private:
	struct FPendingAdmission
	{
		FOnPreLoginCompleteDelegate Completion;
		FString PlatformId;
	};

	struct FValidatedAdmission
	{
		FString DungeonSessionId;
		FString CharacterId;
		FString SteamId;
	};

	TMap<FString, FPendingAdmission> PendingAdmissions;
	TMap<FString, FValidatedAdmission> ValidatedAdmissions;
	TSet<FString> DungeonMemberCharacterIds;
	FString PendingRewardVersion;
	TArray<FRPGCurrencyChange> PendingCurrencyChanges;
	TArray<FRPGDungeonItemReward> PendingItemRewards;
	FTimerHandle DungeonHeartbeatTimer;
	bool bDungeonStartConfirmed = false;
	bool bDungeonFinishRequested = false;
	bool bDungeonFinishReported = false;
	bool bRequestedDungeonCleared = false;
	bool bRewardSettlementRequested = false;
	bool bRewardSettlementRequestInFlight = false;
};
