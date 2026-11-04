#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Http.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Online/RPGDungeonAbandonment.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Type/RPGStructTypes.h"
#include "HttpWebManager.generated.h"

class APlayerController;
class FJsonObject;
struct FRPGDungeonRewardSnapshotResult;
struct FRPGDungeonMembershipSnapshotResult;
struct FRPGCharacterProgressionResult;
struct FRPGCharacterSkillConfiguration;
struct FRPGEquipmentMutationCommand;
struct FRPGEquipmentMutationHttpResult;
struct FRPGNarrativeHttpResult;
class FRPGDungeonDisconnectionJournal;
class FRPGDungeonConsumeJournal;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCharacterCreationOption
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character") FString ClassId;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character") FString Description;
	static bool DecodeList(const FString& Body, TArray<FRPGCharacterCreationOption>& OutOptions);
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBackendCharacter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString RosterId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString CreatedAt;

	/** Optional display projection. Missing/invalid metadata never implies level one. */
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString ProgressionState = TEXT("Unavailable");

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString ClassId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	int32 CharacterLevel = 0;

	static bool Decode(const TSharedPtr<FJsonObject>& Object, FRPGBackendCharacter& OutCharacter);
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonSessionMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString JoinedAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString LeaseExpiresAt;

	/** Optional public character name from the authenticated session response; never identity/authority. */
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString CharacterName;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonSession
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString DungeonSessionId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString DungeonId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString Difficulty;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ServerId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ServerAddress;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") FString LeaderCharacterId;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") int64 LobbyRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") FString DepartureRequestId;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") int64 DepartureRequestRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") FString DepartureRequestedAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ExpiresAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	TArray<FRPGDungeonSessionMember> Members;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventorySaved, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCharacterInventorySaved,
	const FString&, CharacterId,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnInventoryLoaded, const TArray<FItemSaveData>&, LoadedData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnCharacterInventoryLoaded,
	const FString&, CharacterId,
	const TArray<FItemSaveData>&, LoadedData,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnBackendAuthenticationCompleted,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCharactersLoaded,
	const TArray<FRPGBackendCharacter>&, Characters,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCharacterCreated,
	const FRPGBackendCharacter&, Character,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCharacterCreationOptionsLoaded,
	const TArray<FRPGCharacterCreationOption>&, Options, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnDungeonSessionUpdated,
	const FRPGDungeonSession&, DungeonSession,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FOnJoinTicketIssued,
	const FString&, DungeonSessionId,
	const FString&, CharacterId,
	const FString&, JoinTicket,
	const FString&, ExpiresAt,
	bool, bSuccess);
// Internal detailed completion; the existing Blueprint delegate keeps its signature.
DECLARE_MULTICAST_DELEGATE_SixParams(FOnJoinTicketResult,
	const FString&, const FString&, const FString&, const FString&, bool, const FString&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnDungeonRewardSettlementRequested,
	const FString&, State,
	int32, HttpStatus,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(
	FOnJoinTicketConsumed,
	const FString&, JoinTicket,
	const FString&, DungeonSessionId,
	const FString&, CharacterId,
	const FString&, SteamId,
	int64, ConnectionGeneration,
	bool, bSuccess);

/**
 * Thin HTTP transport used by the authoritative game server.
 * Authentication and database transactions still belong to the backend service.
 */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_RPG_API UHttpWebManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Exchanges a Steam Web API auth ticket (hex encoded) for a backend token.
	 * Ticket creation remains in the Steam/OnlineSubsystem integration layer.
	 */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Authentication")
	void AuthenticateWithSteamTicket(const FString& SteamTicketHex);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Authentication")
	void SetAccessToken(const FString& InAccessToken);

	/** Clears the credential only. Account binding and selected gameplay state are not a logout. */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Authentication")
	void ClearAccessToken();

	UFUNCTION(BlueprintCallable, Category = "HTTP|Character")
	void LoadCharacters();
	UFUNCTION(BlueprintCallable, Category = "HTTP|Character")
	void LoadCharacterCreationOptions();

	UFUNCTION(BlueprintCallable, Category = "HTTP|Character")
	void CreateCharacter(const FString& CharacterName, const FString& ClassId = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void CreateDungeonSession(
		const FString& CharacterId,
		const FString& DungeonId,
		const FString& Difficulty);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void JoinDungeonSession(
		const FString& DungeonSessionId,
		const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void LoadDungeonSession(const FString& DungeonSessionId);

	/** Loads the selected character's current resumable dungeon, if any. */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void LoadActiveDungeonSession(const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void LeaveDungeonSession(
		const FString& DungeonSessionId,
		const FString& CharacterId);

	void SetDungeonDeparture(const FString& SessionId, const FString& CharacterId,
		const FString& RequestId, int64 ExpectedRevision, bool bCancel);

	/** Player-only, bodyless and idempotent. Does not clear client state or disconnect an actor. */
	void AbandonDungeonSession(const FString& DungeonSessionId, const FString& CharacterId,
		const FString& ExpectedSteamId);

	/** Discards pre-disconnect client callbacks only; does not undo backend mutations or clear authentication. */
	void InvalidatePlayerDungeonRequests();

	/**
	 * Requests a short-lived, single-use ticket for a character that currently
	 * holds an active lease in the specified dungeon session.
	 */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void RequestJoinTicket(
		const FString& CharacterId,
		const FString& DungeonSessionId);

	/** Dedicated Server only: atomically consumes a client join ticket. */
	void ConsumeJoinTicket(const FString& JoinTicket, const FString& ExpectedSteamId = FString());

	/** Original PreLogin peer only: preserve its first observed socket loss, never a lookup/send timestamp. */
	void ObserveDungeonConsumeConnectionLoss(const FString& JoinTicket, const FString& ExpectedSteamId,
		FDateTime ObservedAtUtc);
	bool HasExpiredDungeonConsumeRecovery(const FString& JoinTicket) const;

	/** False while an observed disconnect has not been durably resolved; reads/heartbeats remain available. */
	bool CanDispatchConfiguredDungeonConnectionMutation() const;

	/** Server-local, bounded read-only JSON. Never sends/retries, reads files or clears a recovery guard. */
	FString BuildConfiguredDungeonRecoveryDiagnostics(int32 MaxIntents = 32) const;

	/** Dedicated Server only: confirms completed InitNewPlayer for the current ticket generation. */
	bool ConfirmConfiguredDungeonMemberAdmission(const FString& DungeonSessionId,
		const FString& CharacterId, const FString& SteamId, int64 ConnectionGeneration,
		TFunction<void(bool, int32)> Completion);

	/** Dedicated Server only: true after durable enqueue of this exact connection generation. */
	bool ReportConfiguredDungeonMemberDisconnection(const FString& DungeonSessionId,
		const FString& CharacterId, const FString& SteamId, int64 ConnectionGeneration,
		FDateTime ObservedAtUtc);

	/** Dedicated Server only: reports that the configured instance is ready. */
	void StartConfiguredDungeonSession();

	/** Dedicated Server only: renews the configured instance and member leases. */
	void HeartbeatConfiguredDungeonSession();

	/** Dedicated Server only: reports the authoritative PvE result. */
	void FinishConfiguredDungeonSession(bool bCleared);

	/** Dedicated Server only: persists and queues the authoritative clear reward. */
	void SettleConfiguredDungeonRewards(
		const FString& RewardVersion,
		const TArray<FRPGCurrencyChange>& CurrencyChanges,
		const TArray<FRPGDungeonItemReward>& ItemRewards);

	/** Read-only, session-bound status/snapshot; also valid in the post-session grace window. */
	bool LoadConfiguredDungeonRewardSnapshot(const FString& CharacterId,
		TFunction<void(FRPGDungeonRewardSnapshotResult)> Completion, const FString& RewardVersion = FString());
	/** Assigned-server credential only; a player cannot supply a restore record to a Pawn. */
	bool LoadConfiguredCharacterProgression(const FString& CharacterId,
		TFunction<void(FRPGCharacterProgressionResult)> Completion);
	/** Player credential only. Frontend eligibility and drafts belong to the lobby. */
	bool LoadPlayerCharacterProgression(const FString& CharacterId, TFunction<void(FRPGCharacterProgressionResult)> Completion);
	bool SavePlayerCharacterConfiguration(const FString& CharacterId, const FRPGCharacterSkillConfiguration& Command,
		TFunction<void(FRPGCharacterProgressionResult)> Completion);
	bool IsPlayerProgressionRequestInFlight() const { return ActivePlayerProgressionRequest.IsValid(); }

	/** Pinned player/endpoint identity only, never exposes a bearer or service credential. */
	bool GetPlayerEquipmentContext(FString& OutEndpoint, FString& OutSteamId) const;
	/** Build-only owner-scoped narrative GET/choice POST; player credentials remain private. */
	FHttpRequestPtr CreatePlayerNarrativeRequest(FGuid CharacterId, const FString& ChoiceBody,
		TFunction<void(FRPGNarrativeHttpResult)> Completion);
	/** Build only; the intent owner must durably save before dispatching this request. */
	FHttpRequestPtr CreatePlayerEquipmentMutationRequest(const FRPGEquipmentMutationCommand& Command,
		TFunction<void(FRPGEquipmentMutationHttpResult)> Completion);
	/** Fresh owner-only read; no terminal history, no server credential. Build only. */
	FHttpRequestPtr CreatePlayerEquipmentItemsRequest(FGuid CharacterId,
		TFunction<void(FRPGEquipmentMutationHttpResult)> Completion);

	/** Single in-flight, read-only GET using only the assigned server credential. */
	bool LoadConfiguredDungeonMembershipSnapshot(
		TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion);

	/** Dedicated server only: process elapsed disconnects, then let the caller read authoritative receipts. */
	bool ProcessConfiguredDungeonDisconnectionExpirations(TFunction<void()> Completion);

	/** Appends the ticket as a URL option and starts the UE connection. */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	static bool ConnectWithJoinTicket(
		APlayerController* PlayerController,
		const FString& ServerAddress,
		const FString& JoinTicket);

	UFUNCTION(BlueprintPure, Category = "HTTP|Authentication")
	bool HasAccessToken() const;
	/** Endpoint identity only; never includes the player's credential. */
	const FString& GetApiUrl() const { return ApiUrl; }

	/** Native transport; the lobby coordinator owns gameplay/UI eligibility. */
	bool RequestPlayerLogout(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion);
	bool IsPlayerLogoutInFlight() const { return ActivePlayerLogoutRequest.IsValid(); }
	bool HasPlayerLogoutCredential() const { return !PlayerAuth.LogoutAuthorization().IsEmpty(); }

	/** Account remains pinned across credential expiry; switching accounts needs an explicit logout flow. */
	const FString& GetAuthenticatedSteamId() const { return PlayerAuth.GetSteamId(); }

	const FString& GetGameServerId() const { return GameServerId; }
	const FString& GetConfiguredDungeonSessionId() const
	{
		return ConfiguredDungeonSessionId;
	}
	bool IsConfiguredForDungeonServer() const
	{
		return !BackendServiceToken.IsEmpty()
			&& !GameServerId.IsEmpty()
			&& !ConfiguredDungeonSessionId.IsEmpty();
	}

	void OnAuthenticateResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful);
	void OnLoadCharactersResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful);
	void OnCreateCharacterResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful);
	void OnDungeonSessionResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString Operation,
		FString ExpectedDungeonSessionId,
		uint64 RequestGeneration = 0);
	void OnRequestJoinTicketResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString CharacterId,
		FString DungeonSessionId,
		uint64 RequestGeneration = 0);
	void OnConsumeJoinTicketResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString JoinTicket);
	void OnSettleDungeonRewardsResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString ExpectedRewardVersion);
	void ApplyCommonHeaders(const FHttpRequestRef& Request) const;

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnInventorySaved OnInventorySaved;

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnCharacterInventorySaved OnCharacterInventorySaved;

	/** Legacy uncorrelated callback. Prefer OnCharacterInventoryLoaded. */
	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnInventoryLoaded OnInventoryLoaded;

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnCharacterInventoryLoaded OnCharacterInventoryLoaded;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Authentication")
	FOnBackendAuthenticationCompleted OnBackendAuthenticationCompleted;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Character")
	FOnCharactersLoaded OnCharactersLoaded;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Character")
	FOnCharacterCreated OnCharacterCreated;
	UPROPERTY(BlueprintAssignable, Category = "HTTP|Character")
	FOnCharacterCreationOptionsLoaded OnCharacterCreationOptionsLoaded;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Dungeon Session")
	FOnDungeonSessionUpdated OnDungeonSessionUpdated;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Dungeon Session")
	FOnDungeonAbandonmentCompleted OnDungeonAbandonmentCompleted;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Dungeon Session")
	FOnJoinTicketIssued OnJoinTicketIssued;
	FOnJoinTicketResult OnJoinTicketResult;

	UPROPERTY()
	FOnJoinTicketConsumed OnJoinTicketConsumed;

	UPROPERTY()
	FOnDungeonRewardSettlementRequested OnDungeonRewardSettlementRequested;

private:
	void OnCharacterCreationOptionsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
		bool bWasSuccessful, uint64 Generation);
	uint64 CharacterCreationOptionsGeneration = 0;
	void OnPlayerLogoutResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void BroadcastJoinTicketResult(const FString& DungeonSessionId, const FString& CharacterId,
		const FString& JoinTicket, const FString& ExpiresAt, bool bSuccess, const FString& ErrorCode = FString());
	friend struct FRPGBackendAuthTestAccess;
	friend struct FRPGEquipmentMutationTestAccess;
	friend struct FRPGNarrativeTestAccess;
	friend class FRPGMenuFrontendTest;
	friend struct FRPGDungeonDepartureJournalTestAccess;
	friend struct FRPGSteamLobbyPublicationTestAccess;
	friend struct FRPGDungeonAbandonmentTestAccess;
	friend struct FRPGDungeonMembershipTestAccess;
	friend struct FRPGDungeonDisconnectionOrderingTestAccess;
	FHttpRequestPtr CreateConsumeJoinTicketRequest(const FString& JoinTicket, const FString& ExpectedSteamId = FString());
	FHttpRequestPtr CreateDungeonRewardSettlementRequest(const FString& RewardVersion,
		const TArray<FRPGCurrencyChange>& CurrencyChanges, const TArray<FRPGDungeonItemReward>& ItemRewards);
	struct FDungeonMutationScope
	{
		FString BackendUrl, ServerId, SessionId;
		TWeakObjectPtr<UWorld> World;
	};
	struct FDungeonConsumeRecovery
	{
		enum class EEvidenceOutcome : uint8
		{
			NotChecked, TransportFailure, HttpRejected, InvalidResponse, Unresolved,
			ConsumptionRecorded, CancellationVerified
		};
		FDungeonMutationScope Scope;
		FString RequestId, Body; // Memory only; never log a ticket or persist service credentials.
		FString ExpectedSteamId, TicketHash, EvidenceBody, EvidenceCharacterId;
		FDateTime LostAtUtc;
		int64 EvidenceGeneration = 0;
		FHttpRequestPtr EvidenceRequest;
		double Deadline = 0.0, NextAttemptAt = 0.0;
		int32 Retries = 0;
		bool bWaiting = false, bUncertain = false, bExpired = false;
		bool bRestoredWorldPending = false;
		bool bSealRequested = false, bCancellationVerified = false;
		// Diagnostic observations only: never persisted or used as admission/cancellation authority.
		EEvidenceOutcome LastEvidenceOutcome = EEvidenceOutcome::NotChecked;
		int32 LastEvidenceHttpStatus = 0;
		FDateTime LastEvidenceCompletedAtUtc;
		bool bLastEvidenceWasSeal = false;
	};
	FHttpRequestPtr CreateDungeonConsumeAttempt(const FString& Ticket, const TSharedRef<FDungeonConsumeRecovery>& Recovery);
	FHttpRequestPtr TakeDueDungeonConsumeRetry(const FString& Ticket, double Now);
	void DispatchDungeonConsumeAttempt(const FString& Ticket, const FHttpRequestPtr& Request);
	void ExpireDungeonConsumeRecovery(const FString& Ticket, const TSharedRef<FDungeonConsumeRecovery>& Recovery);
	FHttpRequestPtr TakeDueDungeonConsumeEvidence(const FString& Ticket, double Now);
	void DispatchDungeonConsumeEvidence(const FString& Ticket, const FHttpRequestPtr& Request);
	void CompleteDungeonConsumeEvidence(const FString& Ticket, const TSharedRef<FDungeonConsumeRecovery>& Recovery,
		const FHttpRequestPtr& Request, const FHttpResponsePtr& Response, bool bTransport);
	bool QueueDungeonConsumeEvidence(const FString& Ticket, const TSharedRef<FDungeonConsumeRecovery>& Recovery);
	bool ResolveCancelledDungeonConsumeRecovery(const FString& Ticket, const TSharedRef<FDungeonConsumeRecovery>& Recovery);
	void TickDungeonConsumeRecoveries();
	void InitializeDungeonConsumeRecoveries(const FString& Directory);
	bool RestoreDungeonConsumeRecoveries();
	void BindRestoredDungeonConsumeWorld();
	void PersistDungeonConsumeObservation(const TSharedRef<FDungeonConsumeRecovery>& Recovery);
	void RetryDungeonConsumeObservationWrites(double Now);
	TSharedPtr<FRPGDungeonConsumeJournal> DungeonConsumeJournal;
	// Keep failed observations independently of HTTP/admission callback ownership.
	TMap<FString, TSharedPtr<FDungeonConsumeRecovery>> PendingDungeonConsumeObservationWrites;
	double NextDungeonConsumeObservationWriteAt = 0.0;
	int32 DungeonConsumeObservationWriteRetries = 0;
	// Unattributed/contradictory failures remain sticky; an empty retry queue cannot clear them.
	bool bDungeonConsumePersistenceFailed = false;
	bool IsDungeonDisconnectionOutboxReady() const;
	TMap<FString, TSharedPtr<FDungeonConsumeRecovery>> DungeonConsumeRecoveries;
	// Uncertain intents remain until durable handoff or verified cancellation AND journal cleanup.
	bool bDungeonConsumeRecoveryCompleting = false;
	FDungeonMutationScope CaptureDungeonMutationScope() const;
	bool IsCurrentDungeonMutationScope(const FDungeonMutationScope& Scope) const;
	void CancelDungeonConnectionMutations();
	TMap<FString, FHttpRequestPtr> ActiveDungeonConsumeRequests;
	FHttpRequestPtr ActiveDungeonSettlementRequest;
	FHttpRequestPtr ActivePlayerProgressionRequest;
	bool RequestPlayerProgression(const FString& CharacterId, const FRPGCharacterSkillConfiguration* Command,
		TFunction<void(FRPGCharacterProgressionResult)> Completion);
	struct FDungeonDisconnectionReport
	{
		FString DungeonSessionId;
		FString CharacterId;
		FString SteamId;
		FString Body;
		int64 ConnectionGeneration = 0;
		FDateTime ObservedAtUtc;
		int32 Attempts = 0;
		double NextAttemptAt = 0.0;
		FHttpRequestPtr Request;
	};
	struct FDungeonDisconnectionWrite
	{
		FDungeonMutationScope Scope;
		TSharedPtr<FDungeonDisconnectionReport> Report;
	};
	bool PersistDungeonDisconnectionWrite(const FString& Key, const FDungeonDisconnectionWrite& Write);
	void RetryDungeonDisconnectionWrites(double Now);
	TMap<FString, FDungeonDisconnectionWrite> PendingDungeonDisconnectionWrites;
	double NextDungeonDisconnectionWriteAt = 0.0;
	int32 DungeonDisconnectionWriteRetries = 0;
	void InitializeDungeonDisconnectionReports();
	void InitializeDungeonDisconnectionReports(const FString& Directory);
	bool RestoreDungeonDisconnectionReports();
	void RetryDungeonJournalRestores(double Now);
	// Startup restore is distinct from a later, attributable write failure. Never
	// replace a loaded journal or replay its entries again after an HTTP ACK.
	bool bDungeonDisconnectionRestorePending = false;
	bool bDungeonConsumeRestorePending = false;
	double NextDungeonJournalRestoreAt = 0.0;
	int32 DungeonJournalRestoreRetries = 0;
	bool TickDungeonDisconnectionReports(float DeltaTime);
	TSharedPtr<FRPGDungeonDisconnectionJournal> DungeonDisconnectionJournal;
	TMap<FString, TSharedPtr<FDungeonDisconnectionReport>> DungeonDisconnectionReports;
	FTSTicker::FDelegateHandle DungeonDisconnectionTicker;
	bool SendDungeonDisconnectionAttempt(const TSharedRef<FDungeonDisconnectionReport>& Report);
	FHttpRequestPtr CreateDungeonDisconnectionAttempt(const TSharedRef<FDungeonDisconnectionReport>& Report);
	bool ScheduleDungeonDisconnectionRetry(const TSharedRef<FDungeonDisconnectionReport>& Report,
		int32 CompletedAttempt, int32 HttpStatus);
	bool bDungeonDisconnectionReportShutdown = false;
	// Unattributed/contradictory failures remain sticky; tracked I/O retries are separate.
	bool bDungeonDisconnectionPersistenceFailed = false;
	FHttpRequestPtr CreateConfiguredDungeonMembershipRequest(
		TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion);
	FHttpRequestPtr CreateConfiguredDungeonDisconnectionExpirationsRequest(TFunction<void()> Completion);
	void CancelDungeonMembershipRequest();
	FHttpRequestPtr ActiveDungeonMembershipRequest;
	uint64 DungeonMembershipRequestGeneration = 0;
	bool bDungeonMembershipTransportShutdown = false;
	void OnAbandonDungeonSessionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
		bool bWasSuccessful, FString SessionId, FString CharacterId, FString ExpectedSteamId,
		uint64 RequestGeneration);
	FHttpRequestRef CreateDungeonAbandonmentRequest(const FString& SessionId,
		const FString& CharacterId, const FString& ExpectedSteamId);
	UPROPERTY(Config, EditAnywhere, Category = "HTTP")
	FString ApiUrl = TEXT("http://localhost:3000/api");

	UPROPERTY(Config, EditAnywhere, Category = "HTTP", meta = (ClampMin = "1.0"))
	float RequestTimeoutSeconds = 10.f;

	/** Kept in memory only; never persist this token in a packaged client config. */
	FRPGBackendPlayerAuthState PlayerAuth;
	uint64 PlayerDungeonRequestGeneration = 0;
	FHttpRequestPtr ActiveAuthenticationRequest;
	FHttpRequestPtr ActivePlayerLogoutRequest;
	TFunction<bool()> PlayerLogoutContextValidator;
	TFunction<void(bool)> PlayerLogoutCompletion;
	double ActiveAuthenticationRequestStartedSeconds = -1.0;
	bool AcceptPlayerResponse(const FHttpRequestPtr& Request, const FHttpResponsePtr& Response,
		bool& bWasSuccessful);

	/** Loaded from PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN on a dedicated server only. */
	FString BackendServiceToken;

	/** Stable ID assigned to this process by the game-server allocator. */
	FString GameServerId;

	/** Dungeon instance assigned to this process by the game-server allocator. */
	FString ConfiguredDungeonSessionId;

	void SendDungeonSessionRequest(
		const FString& RelativePath,
		const FString& Verb,
		const TSharedPtr<FJsonObject>& RequestObject,
		const FString& Operation,
		const FString& ExpectedDungeonSessionId);

	void SendConfiguredDungeonSessionRequest(
		const FString& Action,
		const FString& Outcome = FString());
};
