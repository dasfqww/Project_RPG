#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Manager/HttpWebManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGDungeonSessionSubsystem.generated.h"

class APlayerController;
class UNetDriver;
class UNetConnection;
struct FRPGDungeonDepartureCommand;

UENUM(BlueprintType)
enum class ERPGDungeonClientFlowState : uint8
{
	Idle,
	CreatingSession,
	JoiningSession,
	InSession,
	RefreshingSession,
	RequestingJoinTicket,
	Connecting,
	LeavingSession,
	Error,
	ResumingSession,
	ReturningToLobby,
	AbandoningSession
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FRPGDungeonClientFlowStateChanged,
	ERPGDungeonClientFlowState, NewState,
	const FString&, ErrorCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGDungeonClientSessionChanged,
	const FRPGDungeonSession&, DungeonSession);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGDungeonClientTravelStarted,
	const FString&, ServerAddress);

/**
 * Blueprint-facing coordinator for the client dungeon flow.
 *
 * UHttpWebManager remains the low-level HTTP transport. This subsystem owns
 * request ordering and the selected-character/current-session state so UI
 * Blueprints do not need to correlate unrelated HTTP delegates themselves.
 */
UCLASS(Config = Game)
class PROJECT_RPG_API URPGDungeonSessionSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool SelectCharacter(const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool CreateDungeonSession(
		const FString& DungeonId,
		const FString& Difficulty);

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool JoinDungeonSession(const FString& DungeonSessionId);

	/** Restores the selected character's active session after a restart. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool ResumeDungeonSession();

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool RefreshDungeonSession();

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool LeaveDungeonSession();

	UFUNCTION(BlueprintPure, Category = "Online|Dungeon") bool CanSetDeparture(bool bCancel) const;
	UFUNCTION(BlueprintCallable, Category = "Online|Dungeon") bool SetDeparture(bool bCancel);
	UFUNCTION(BlueprintPure, Category = "Online|Dungeon") bool HasUnresolvedDeparture() const { return !PendingDepartureRequest.IsEmpty(); }
	UFUNCTION(BlueprintPure, Category = "Online|Dungeon") bool IsPendingDepartureCancellation() const { return HasUnresolvedDeparture() && bPendingDepartureCancel; }
	/** Reuses the captured account/character/session, command ID, revision and intent. */
	UFUNCTION(BlueprintCallable, Category = "Online|Dungeon") bool RetryDeparture();
	UFUNCTION(BlueprintPure, Category = "Online|Dungeon") bool HasDepartureRecoveryError() const { return bDepartureRecoveryBlocked; }

	/** Development-only lobby opt-out, with the identity captured by a separate confirmation UI. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool AbandonDungeonSession(APlayerController* PlayerController, const FString& ExpectedSessionId,
		const FString& ExpectedCharacterId, const FString& ExpectedSteamId);

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool CanAbandonDungeonSession() const;

	/** Development-only defeat action: disconnect first, then submit the confirmed identity in the lobby. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool QueueDungeonAbandonmentFromResult(APlayerController* PlayerController, int32 ExpectedBattleRevision,
		const FString& ExpectedSessionId, const FString& ExpectedCharacterId, const FString& ExpectedSteamId);

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool CanQueueDungeonAbandonmentFromResult(APlayerController* PlayerController) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool HasQueuedDungeonAbandonment() const { return bAbandonmentQueuedForLobby; }

	bool IsResultAbandonmentTestingEnabled() const;

	/** Development-only combat consent; the POST still waits for the disconnected lobby. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool QueueDungeonAbandonmentFromGameplay(APlayerController* PlayerController, int32 ExpectedBattleRevision,
		const FString& ExpectedSessionId, const FString& ExpectedCharacterId, const FString& ExpectedSteamId);
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool CanQueueDungeonAbandonmentFromGameplay(APlayerController* PlayerController) const;
	bool IsGameplayAbandonmentTestingEnabled() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool HasUnresolvedDungeonAbandonment() const { return !AbandonmentSessionId.IsEmpty(); }

	/** Kept closed in Shipping until authoritative actor ejection/admission revalidation is implemented. */
	bool IsAbandonmentTestingEnabled() const;

	/**
	 * Requests a session-bound join ticket and travels on success.
	 * ServerAddress is expected to come from the authenticated backend dungeon
	 * session after it reaches Loading or InProgress.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool ConnectToDungeon(
		APlayerController* PlayerController,
		const FString& ServerAddress);

	/** Refreshes backend state; only Cleared/Failed sessions may return locally. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool ReturnToLobby(APlayerController* PlayerController);

	/** Local escape only: preserves backend membership, rewards and account binding. Not abandonment. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool DisconnectToLobby(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	void ResetDungeonFlow(bool bKeepSelectedCharacter = true);

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	ERPGDungeonClientFlowState GetFlowState() const { return FlowState; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	const FString& GetSelectedCharacterId() const
	{
		return SelectedCharacterId;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	FRPGDungeonSession GetCurrentDungeonSession() const
	{
		return CurrentDungeonSession;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	const FString& GetLastErrorCode() const { return LastErrorCode; }

	/** Local UI guard for this exact session/character; never an abandonment receipt. */
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	FString GetDungeonAdmissionBlockReason() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool IsRequestInFlight() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool HasActiveDungeonSession() const;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Dungeon")
	FRPGDungeonClientFlowStateChanged OnFlowStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Dungeon")
	FRPGDungeonClientSessionChanged OnDungeonSessionChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Dungeon")
	FRPGDungeonClientTravelStarted OnDungeonTravelStarted;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Dungeon")
	FRPGDungeonClientTravelStarted OnLobbyTravelStarted;

private:
	friend struct FRPGDungeonFlowTestAccess;
	friend struct FRPGDungeonDepartureJournalTestAccess;
	friend struct FRPGSteamLobbyPublicationTestAccess;
	friend struct FRPGDungeonAbandonmentTestAccess;
	friend class URPGDungeonRecoveryTestSubsystem;

	/** A local map package, never a server URL or a URL carrying join options. */
	UPROPERTY(Config)
	FString LobbyMapPackage = TEXT("/Game/Maps/LobbyMap");

	enum class EPendingOperation : uint8
	{
		None,
		Create,
		Join,
		Refresh,
		RecheckMembership,
		Leave,
		Departure,
		Connect,
		Resume,
		Return,
		Abandon
	};

	UFUNCTION()
	void HandleDungeonSessionUpdated(
		const FRPGDungeonSession& DungeonSession,
		bool bSuccess);

	void HandleJoinTicketIssued(
		const FString& DungeonSessionId,
		const FString& CharacterId,
		const FString& JoinTicket,
		const FString& ExpiresAt,
		bool bSuccess, const FString& ErrorCode = FString());

	UFUNCTION()
	void HandleDungeonAbandonmentCompleted(const FRPGDungeonAbandonmentReceipt& Receipt,
		bool bSuccess, const FString& ErrorCode);
	bool BeginAbandonment(APlayerController* PlayerController, const FString& ExpectedSessionId,
		const FString& ExpectedCharacterId, const FString& ExpectedSteamId);
	bool RejectUnresolvedAbandonment();
	void ClearAbandonmentIdentity();
	bool CanQueueDungeonAbandonmentFromBattle(APlayerController* PlayerController, bool bFromResult) const;
	bool QueueDungeonAbandonmentFromBattle(APlayerController* PlayerController, int32 ExpectedBattleRevision,
		const FString& ExpectedSessionId, const FString& ExpectedCharacterId, const FString& ExpectedSteamId, bool bFromResult);

	bool BeginOperation(
		EPendingOperation Operation,
		ERPGDungeonClientFlowState RequestState);
	bool ValidateSelectedCharacter();
	bool CurrentSessionContainsSelectedCharacter() const;
	void CompleteSessionOperation(const FRPGDungeonSession& DungeonSession);
	void FailOperation(const FString& ErrorCode);
	void RejectRequest(const FString& ErrorCode);
	void SetFlowState(
		ERPGDungeonClientFlowState NewState,
		const FString& ErrorCode = FString());
	void BroadcastSessionChanged();
	bool IsValidLocalController(const APlayerController* PlayerController) const;
	void CompleteLobbyReturn(const FRPGDungeonSession& DungeonSession);
	void HandleLobbyMapLoaded(UWorld* LoadedWorld);
	void HandleLobbyTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);
	void HandleDungeonNetworkFailure(UWorld* World, UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType, const FString& Error);
	bool IsLobbyMapAvailable() const;
	bool QueueLocalRecovery(UWorld* World, const FString& ReasonCode, APlayerController* PlannedController = nullptr);
	bool TickLocalRecovery(float DeltaSeconds);
	void CancelLocalRecovery();
	void ClearRejectedDungeonAdmission();
	bool RestoreDepartureCommand();
	bool PersistDepartureCommand();
	bool ResolveDepartureCommand();
	FRPGDungeonDepartureCommand GetPendingDepartureCommand() const;
	FString GetDepartureRecoveryDirectory() const;
	bool IsDepartureScopeCurrent() const;

	UPROPERTY(Transient)
	TObjectPtr<UHttpWebManager> WebManager;

	UPROPERTY(Transient)
	FRPGDungeonSession CurrentDungeonSession;

	FString SelectedCharacterId;
	FString LastErrorCode;
	FString RejectedAdmissionSessionId;
	FString RejectedAdmissionCharacterId;
	FString RejectedAdmissionReason;
	FString PendingServerAddress;
	// No credential/ticket is persisted. A local hint still requires fresh authenticated backend reads.
	FString PendingDepartureBackend;
	FString DepartureRecoveryDirectory;
	bool bDepartureRecoveryBlocked = false;
	FString PendingDepartureSession;
	FString PendingDepartureCharacter;
	FString PendingDepartureSteam;
	FString PendingDepartureRequest;
	int64 PendingDepartureRevision = 0;
	bool bPendingDepartureCancel = false;
	FString PendingReturnSessionId;
	FString LocalRecoveryReason;
	// Memory-only uncertainty marker. Local recovery/expiry must not reset it.
	FString AbandonmentSessionId;
	FString AbandonmentCharacterId;
	FString AbandonmentSteamId;
	bool bAbandonmentTestingEnabled = false;
	bool bResultAbandonmentTestingEnabled = false;
	bool bGameplayAbandonmentTestingEnabled = false;
	bool bAbandonmentQueuedForLobby = false;
	TWeakObjectPtr<UWorld> LocalRecoveryWorld;
	TWeakObjectPtr<UNetConnection> PlannedDisconnectConnection;
	double PlannedDisconnectDeadline = 0.0;
	FTSTicker::FDelegateHandle LocalRecoveryTicker;
	TWeakObjectPtr<APlayerController> PendingPlayerController;
	EPendingOperation PendingOperation = EPendingOperation::None;
	ERPGDungeonClientFlowState FlowState =
		ERPGDungeonClientFlowState::Idle;
};
