#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Http.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UObject;
class UWorld;
class UGameInstance;
class FRPGDungeonConsumeJournal;
class FRPGDungeonDisconnectionJournal;

/** Live assignment/credential access and synchronous admission notification only. */
class IRPGDungeonRecoveryHost
{
public:
	virtual ~IRPGDungeonRecoveryHost() = default;
	virtual UObject& GetRecoveryOwner() const = 0;
	virtual UGameInstance* GetRecoveryGameInstance() const = 0;
	virtual const FString& GetRecoveryApiUrl() const = 0;
	virtual const FString& GetRecoveryServerId() const = 0;
	virtual const FString& GetRecoverySessionId() const = 0;
	virtual bool HasRecoveryCredential() const = 0;
	virtual bool HasValidRecoveryServiceCredential() const = 0;
	virtual bool IsRecoverySettlementActive() const = 0;
	virtual void ApplyRecoveryServiceAuthorization(const FHttpRequestRef& Request) const = 0;
	virtual float GetRecoveryRequestTimeout() const = 0;
	virtual bool IsRecoveryServerRuntime() const = 0;
	virtual bool IsRecoveryServerConfigured() const = 0;
	virtual void ApplyRecoveryHeaders(const FHttpRequestRef& Request) const = 0;
	virtual void BroadcastConsumedTicket(const FString& Ticket, const FString& Session,
		const FString& Character, const FString& Steam, int64 Generation, bool bSuccess) = 0;
};

/** Owns the durable connection-loss barrier, consume reconciliation and retry lifecycle.
 * Host outlives this service. HTTP/ticker callbacks are weakly bound to the host UObject;
 * Shutdown unbinds requests before releasing state. Credentials never enter either journal.
 */
class PROJECT_RPG_API FRPGDungeonConnectionRecovery
{
	friend struct FRPGDungeonDisconnectionOrderingTestAccess;
public:
	explicit FRPGDungeonConnectionRecovery(IRPGDungeonRecoveryHost& InHost) : Host(InHost) {}
	~FRPGDungeonConnectionRecovery();
	FRPGDungeonConnectionRecovery(const FRPGDungeonConnectionRecovery&) = delete;
	FRPGDungeonConnectionRecovery& operator=(const FRPGDungeonConnectionRecovery&) = delete;
	void Initialize();
	void Shutdown();
	void CancelDungeonConnectionMutations();
	struct FDungeonMutationScope
	{
		FString BackendUrl, ServerId, SessionId;
		TWeakObjectPtr<UWorld> World;
	};

	FDungeonMutationScope CaptureDungeonMutationScope() const;
	bool IsCurrentDungeonMutationScope(const FDungeonMutationScope& Scope) const;
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

private:
	IRPGDungeonRecoveryHost& Host;
	FHttpRequestPtr CreateConsumeJoinTicketRequest(const FString& JoinTicket, const FString& ExpectedSteamId = FString());
	void OnConsumeJoinTicketResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString JoinTicket);
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

	TMap<FString, FHttpRequestPtr> ActiveDungeonConsumeRequests;

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

};
