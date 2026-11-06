// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "GameFramework/GameModeBase.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameModeBase.generated.h"

class URPGItemBase;
class UDataTable;
class ARPGPlayer;
class URPGContentClearPanel;
class URPGDungeonRewardDefinition;
class URPGDungeonServerDefinition;
class FDataValidationContext;
class UNetConnection;
class UNetDriver;
class ARPGPlayerState;

UENUM(BlueprintType)
enum class EGameModeType : uint8
{
	Village UMETA(DisplayName = "Village"),
	Content UMETA(DisplayName = "Content")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGBackendDungeonDifficultyConfirmed,
	ERPGGameDifficulty, Difficulty);

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
	virtual void Tick(float DeltaSeconds) override;
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
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Native owning-controller RPC target. Marks only the socket it closes now. */
	bool RequestPlannedDungeonLobbyDisconnect(APlayerController* Controller);
    /** Exact current socket/PlayerState/identity generation confirmed by the backend. */
    bool HasConfirmedDungeonAdmission(const ARPGPlayerState* State) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(
		FDataValidationContext& Context) const override;
#endif

	UFUNCTION(BlueprintCallable)
	void SetGameDifficulty(ERPGGameDifficulty InGameDifficulty);
	/** Online servers are configured by the authenticated backend start response, not this setter. */
	UFUNCTION(BlueprintPure, Category = "Online|Dungeon")
	bool IsBackendDungeonDifficultyReady() const { return bDungeonStartConfirmed; }
	UFUNCTION(BlueprintPure, Category = "Online|Dungeon")
	FString GetConfirmedDungeonContentId() const { return ConfirmedDungeonContentId; }
	/** Server BP can initialize difficulty-dependent encounter content after this one-shot event. */
	UPROPERTY(BlueprintAssignable, Category = "Online|Dungeon")
	FRPGBackendDungeonDifficultyConfirmed OnBackendDungeonDifficultyConfirmed;
	static bool TryParseBackendDungeonDifficulty(const FString& Value, ERPGGameDifficulty& OutDifficulty);
	/** Native server read-only presentation. Never an admission grant or a client RPC. */
	bool GetDungeonMembershipPresentation(const FString& ExpectedSessionId, FRPGDungeonMembershipPresentation& Out) const;
	bool IsDungeonPartyMemberRevoked(const FString& SessionId, const FString& CharacterId, const FString& SteamId) const;

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
	virtual void ReportConfiguredDungeonClear();

protected:
	UFUNCTION()
	void HandleJoinTicketConsumed(
		const FString& JoinTicket,
		const FString& DungeonSessionId,
		const FString& CharacterId,
		const FString& SteamId,
		int64 ConnectionGeneration,
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
	void FailDungeonForInvalidReward(const FString& Reason);
	void PollCompletedDungeonRewards();
	void PollDungeonMembership();
	bool IsDungeonAdmissionAbandoned(const FString& SessionId,
		const FString& CharacterId, const FString& SteamId) const;
	/** Empty unless an exact immutable opt-out or disconnection-expiry receipt is cached. */
	FString GetDungeonAdmissionRevocationReason(const FString& SessionId,
		const FString& CharacterId, const FString& SteamId) const;
	/** Native receipt-gated close; lets net-driver cleanup own Pawn destruction and Logout. */
	bool RequestDungeonMemberEjection(APlayerController* Controller);

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

	/** Separate from heartbeat: Closed opt-out sessions retain only scoped GET authority. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Online|Dungeon", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float DungeonMembershipPollIntervalSeconds = 5.f;

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
	/** Trusted lookup by the authenticated backend dungeon ID. Empty retains unconverted legacy GameModes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Online|Dungeon|Content")
	TArray<TObjectPtr<URPGDungeonServerDefinition>> DungeonServerDefinitions;

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
	UFUNCTION(BlueprintPure, Category = "Game Settings")
	ERPGGameDifficulty GetCurrentGameDifficulty() const { return CurrentGameDifficulty; }
	FORCEINLINE const TArray<TObjectPtr<URPGItemBase>>& GetRewardItems() const { return RewardItems; }

private:
	friend struct FRPGDungeonDifficultyTestAccess;
	friend struct FRPGDungeonServerDefinitionTestAccess;
	void ApplyGameDifficulty(ERPGGameDifficulty InGameDifficulty);
	bool ApplyBackendDungeonConfiguration(const FRPGDungeonSession& Session,
		const FString& ExpectedSessionId, const FString& ExpectedServerId);
	friend struct FRPGDungeonMembershipTestAccess;
	friend struct FRPGDungeonPartyCombatTestAccess;
	friend struct FRPGDungeonEjectionTestAccess;
	friend struct FRPGDungeonConnectionTestAccess;
#if !UE_BUILD_SHIPPING
	friend class ARPGDungeonEjectionNetworkGameMode;
	friend class URPGDungeonFlowE2EServer;
#endif
	FRPGDungeonMembershipMonitor DungeonMembershipMonitor;
	FRPGDungeonMembershipPresentation DungeonMembershipPresentation;
	FTimerHandle DungeonMembershipTimer;
	bool CompleteDungeonMembershipRead(uint64 ReadSerial, const FRPGDungeonMembershipSnapshotResult& Result,
		double RequestStartedAtSeconds = 0.0);
	int32 ApplyCachedDungeonRevocations();
	/** Receipt-gated rejection before backend confirmation publishes any identity/Pawn. */
	bool RequestPendingDungeonAdmissionEjection(APlayerController* Controller);
	struct FPreLoginConnectionState
	{
		FString Ticket; // Memory only; never written to the disconnection journal.
		TWeakObjectPtr<UNetConnection> Connection;
		TWeakObjectPtr<UNetDriver> Driver;
		FUniqueNetIdRepl PlayerId;
		FDateTime LostAtUtc;
		double NextReportAttemptAt = 0.0;
		bool bLossDurablyQueued = false;
	};
	UNetConnection* FindDungeonPreLoginConnection(const FString& Ticket, const FUniqueNetIdRepl& PlayerId) const;
	bool IsDungeonPreLoginScopeCurrent(const FPreLoginConnectionState& State) const;
	bool IsDungeonPreLoginConnectionCurrent(const FPreLoginConnectionState& State) const;
	bool ObserveDungeonPreLoginLoss(FPreLoginConnectionState& State) const;
	void PollDungeonPreLoginConnections();
	void RetainExpiredDungeonPreLogin(const FString& Ticket);
	struct FPendingAdmission
	{
		FOnPreLoginCompleteDelegate Completion;
		FString PlatformId;
		TSharedPtr<FPreLoginConnectionState> PreLoginConnection;
		bool bConsumeRecoveryExpired = false;
	};

	struct FValidatedAdmission
	{
		FString DungeonSessionId;
		FString CharacterId;
		FString SteamId;
		int64 ConnectionGeneration = 0;
		TSharedPtr<FPreLoginConnectionState> PreLoginConnection;
	};
	bool QueueLostDungeonPreLoginAdmission(const FValidatedAdmission& Admission);
	struct FAdmissionRequestState
	{
		// Set only by cleanup of the exact remote connection while HTTP is pending.
		bool bConnectionLostBeforeResponse = false;
		FDateTime ConnectionLostAtUtc;
		bool bConnectionLossDurablyQueued = false;
	};
	struct FAdmissionAwaitingBackend
	{
		FValidatedAdmission Admission;
		TWeakObjectPtr<ARPGPlayerState> PlayerState;
		TWeakObjectPtr<UNetConnection> Connection;
		TSharedPtr<FAdmissionRequestState> RequestState;
	};
	struct FActivatedAdmission
	{
		FValidatedAdmission Admission;
		TWeakObjectPtr<ARPGPlayerState> PlayerState;
		TWeakObjectPtr<UNetConnection> Connection;
		bool bPlannedLobbyDisconnect = false;
	};
	bool IsOwnedDungeonConnectionCleanup(APlayerController* Controller,
		UNetConnection* Connection, ARPGPlayerState* State) const;
	bool GetUnexpectedDungeonConnectionCleanup(APlayerController* Controller,
		FValidatedAdmission& OutAdmission) const;
	void CompleteDungeonAdmission(APlayerController* Controller,
		UNetConnection* ExpectedConnection, const FValidatedAdmission& Expected,
		bool bConfirmed, int32 HttpStatus);
	bool CompleteLostDungeonAdmission(const FValidatedAdmission& Expected,
		FDateTime ObservedAtUtc, bool bConfirmed, int32 HttpStatus);

	TMap<FString, FPendingAdmission> PendingAdmissions;
	TMap<FString, FValidatedAdmission> ValidatedAdmissions;
	TMap<TWeakObjectPtr<APlayerController>, FAdmissionAwaitingBackend> AdmissionsAwaitingBackend;
	TMap<TWeakObjectPtr<APlayerController>, FActivatedAdmission> ActivatedAdmissions;
	bool bDungeonConnectionReportingStopped = false;
	TSet<FString> DungeonMemberCharacterIds;
	FString PendingRewardVersion;
	TArray<FRPGCurrencyChange> PendingCurrencyChanges;
	TArray<FRPGDungeonItemReward> PendingItemRewards;
	FTimerHandle DungeonHeartbeatTimer;
	FTimerHandle DungeonRewardRefreshTimer;
	TSet<FString> RewardRefreshOutstanding;
	TSet<FString> RewardRefreshInFlight;
	FString RefreshingRewardVersion;
	int32 RefreshingItemQuantity = 0;
	double RewardRefreshDeadline = 0.0;
	bool bRewardRefreshActive = false;
	bool bDungeonStartConfirmed = false;
	UPROPERTY(Transient) TObjectPtr<const URPGDungeonServerDefinition> ConfirmedDungeonDefinition;
	UPROPERTY(Transient) TObjectPtr<const URPGDungeonRewardDefinition> ConfirmedDungeonRewardDefinition;
	FString ConfirmedDungeonContentId;
	FString ConfirmedDungeonRewardVersion;
	TArray<FRPGCurrencyChange> ConfirmedDungeonCurrencyChanges;
	TArray<FRPGDungeonItemReward> ConfirmedDungeonItemRewards;
	bool bDungeonFinishRequested = false;
	bool bDungeonFinishReported = false;
	bool bRequestedDungeonCleared = false;
	bool bRewardSettlementRequested = false;
	bool bRewardSettlementRequestInFlight = false;
};
