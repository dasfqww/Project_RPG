#include "Online/RPGDungeonConnectionRecovery.h"
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Online/RPGJoinTicketAdmission.h"
#include "Online/RPGDungeonDisconnectionJournal.h"
#include "Online/RPGDungeonConsumeJournal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
	bool ClaimDungeonConsumeCompletion(TMap<FString, FHttpRequestPtr>& Requests,
		const FString& Ticket, const FHttpRequestPtr& Completed)
	{
		const FHttpRequestPtr* Current = Requests.Find(Ticket);
		if (!Current || *Current != Completed) return false;
		Requests.Remove(Ticket);
		return true;
	}

	bool IsDefinitiveDungeonConsumeDenial(const FHttpResponsePtr& Response)
	{
		if (!Response.IsValid() || Response->GetResponseCode() != 401 || Response->GetContentLength() > 4096) return false;
		TSharedPtr<FJsonObject> Error;
		FString Code;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Error)
			&& Error.IsValid() && Error->Values.Num() == 1 && Error->TryGetStringField(TEXT("error"), Code)
			&& Code.Equals(TEXT("join_ticket_not_recoverable"), ESearchCase::CaseSensitive);
	}

}
FRPGDungeonConnectionRecovery::~FRPGDungeonConnectionRecovery()
{
	// Also release callbacks for native fixtures/owners that never initialized a subsystem.
	// Shutdown only touches owned requests/state and does not access the host.
	Shutdown();
}

void FRPGDungeonConnectionRecovery::Initialize()
{
	bDungeonDisconnectionReportShutdown = false;
	bDungeonDisconnectionPersistenceFailed = false;
	bDungeonConsumeRecoveryCompleting = false;
	bDungeonConsumePersistenceFailed = false;
	if (Host.IsRecoveryServerRuntime() && Host.IsRecoveryServerConfigured()) InitializeDungeonDisconnectionReports();
}

void FRPGDungeonConnectionRecovery::Shutdown()
{
	bDungeonDisconnectionReportShutdown = true;
	CancelDungeonConnectionMutations();
	FTSTicker::GetCoreTicker().RemoveTicker(DungeonDisconnectionTicker);
	DungeonDisconnectionTicker.Reset();
	for (auto& Entry : DungeonDisconnectionReports)
	{
		if (!Entry.Value->Request.IsValid()) continue;
		Entry.Value->Request->OnProcessRequestComplete().Unbind();
		Entry.Value->Request->CancelRequest();
		Entry.Value->Request.Reset();
	}
	DungeonDisconnectionReports.Reset();
	DungeonDisconnectionJournal.Reset();
}

void FRPGDungeonConnectionRecovery::ConsumeJoinTicket(const FString& JoinTicket, const FString& ExpectedSteamId)
{
	// A duplicate caller must not fail the original pending admission.
	if (ActiveDungeonConsumeRequests.Contains(JoinTicket) || DungeonConsumeRecoveries.Contains(JoinTicket)) return;
	FHttpRequestPtr Request = CreateConsumeJoinTicketRequest(JoinTicket, ExpectedSteamId);
	if (!Request.IsValid())
	{
		Host.BroadcastConsumedTicket(JoinTicket, FString(), FString(), FString(), 0, false);
		return;
	}
	DispatchDungeonConsumeAttempt(JoinTicket, Request);
}

void FRPGDungeonConnectionRecovery::DispatchDungeonConsumeAttempt(const FString& JoinTicket, const FHttpRequestPtr& Request)
{
	if (Request.IsValid() && Request->ProcessRequest()) return;
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		// ProcessRequest(false) may already have synchronously completed on this thread.
		if (!ClaimDungeonConsumeCompletion(ActiveDungeonConsumeRequests, JoinTicket, Request)) return;
	}
	OnConsumeJoinTicketResponseReceived(Request, nullptr, false, JoinTicket);
}

FRPGDungeonConnectionRecovery::FDungeonMutationScope FRPGDungeonConnectionRecovery::CaptureDungeonMutationScope() const
{
	FDungeonMutationScope Scope;
	Scope.BackendUrl = Host.GetRecoveryApiUrl(); Scope.ServerId = Host.GetRecoveryServerId(); Scope.SessionId = Host.GetRecoverySessionId();
	Scope.World = Host.GetRecoveryGameInstance() ? Host.GetRecoveryGameInstance()->GetWorld() : nullptr;
	return Scope;
}

bool FRPGDungeonConnectionRecovery::IsCurrentDungeonMutationScope(const FDungeonMutationScope& Scope) const
{
	UWorld* CurrentWorld = Host.GetRecoveryGameInstance() ? Host.GetRecoveryGameInstance()->GetWorld() : nullptr;
	return IsInGameThread() && !bDungeonDisconnectionReportShutdown && !Scope.World.IsStale(true)
		&& Scope.World.Get() == CurrentWorld && (!CurrentWorld || !CurrentWorld->bIsTearingDown)
		&& Scope.BackendUrl.Equals(Host.GetRecoveryApiUrl(), ESearchCase::CaseSensitive)
		&& Scope.ServerId.Equals(Host.GetRecoveryServerId(), ESearchCase::CaseSensitive)
		&& Scope.SessionId.Equals(Host.GetRecoverySessionId(), ESearchCase::CaseSensitive);
}

void FRPGDungeonConnectionRecovery::CancelDungeonConnectionMutations()
{
	for (auto& Entry : ActiveDungeonConsumeRequests)
	{
		Entry.Value->OnProcessRequestComplete().Unbind();
		Entry.Value->CancelRequest();
	}
	ActiveDungeonConsumeRequests.Reset();
	for (auto& Entry : DungeonConsumeRecoveries)
	{
		if (!Entry.Value->EvidenceRequest.IsValid()) continue;
		Entry.Value->EvidenceRequest->OnProcessRequestComplete().Unbind();
		Entry.Value->EvidenceRequest->CancelRequest();
		Entry.Value->EvidenceRequest.Reset();
	}
	DungeonConsumeRecoveries.Reset();

}

bool FRPGDungeonConnectionRecovery::CanDispatchConfiguredDungeonConnectionMutation() const
{
	if (!IsDungeonDisconnectionOutboxReady() || bDungeonConsumeRecoveryCompleting || bDungeonConsumePersistenceFailed
		|| !PendingDungeonConsumeObservationWrites.IsEmpty()
		|| !DungeonConsumeJournal || !DungeonConsumeJournal->IsHealthy() || !DungeonConsumeJournal->GetPending().IsEmpty()) return false;
	for (const auto& Entry : DungeonConsumeRecoveries)
		if (Entry.Value->bUncertain) return false;
	return true;
}

bool FRPGDungeonConnectionRecovery::IsDungeonDisconnectionOutboxReady() const
{
	return IsInGameThread() && Host.IsRecoveryServerRuntime() && Host.IsRecoveryServerConfigured()
		&& !bDungeonDisconnectionReportShutdown && !bDungeonDisconnectionPersistenceFailed
		&& !bDungeonDisconnectionRestorePending && !bDungeonConsumeRestorePending
		&& PendingDungeonDisconnectionWrites.IsEmpty()
		&& DungeonDisconnectionJournal && DungeonDisconnectionJournal->IsHealthy()
		&& DungeonDisconnectionJournal->GetPending().IsEmpty() && DungeonDisconnectionReports.IsEmpty();
}

FString FRPGDungeonConnectionRecovery::BuildConfiguredDungeonRecoveryDiagnostics(int32 MaxIntents) const
{
	// No transport, file enumeration, restore/bind, retry or cleanup from this path.
	if (!IsInGameThread() || !Host.IsRecoveryServerRuntime())
		return TEXT("{\"available\":false,\"reason\":\"server_game_thread_required\"}");
	if (bDungeonDisconnectionReportShutdown)
		return TEXT("{\"available\":false,\"reason\":\"shutdown\"}");
	const UWorld* World = Host.GetRecoveryGameInstance() ? Host.GetRecoveryGameInstance()->GetWorld() : nullptr;
	if (World && (World->GetNetMode() == NM_Client || World->bIsTearingDown))
		return TEXT("{\"available\":false,\"reason\":\"server_world_required\"}");
	auto SafeUuid = [](const FString& Value)
	{
		FGuid Id;
		return FGuid::Parse(Value, Id) && Id.IsValid()
			? Id.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	};
	const auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetBoolField(TEXT("available"), true);
	Root->SetStringField(TEXT("dungeonSessionId"), SafeUuid(Host.GetRecoverySessionId()));
	Root->SetBoolField(TEXT("configured"), Host.IsRecoveryServerConfigured());
	Root->SetBoolField(TEXT("credentialPresent"), Host.HasRecoveryCredential());
	Root->SetBoolField(TEXT("mutationsAllowed"), CanDispatchConfiguredDungeonConnectionMutation());
	Root->SetBoolField(TEXT("completionInProgress"), bDungeonConsumeRecoveryCompleting);
	Root->SetNumberField(TEXT("activeConsumes"), ActiveDungeonConsumeRequests.Num());
	Root->SetBoolField(TEXT("activeSettlement"), Host.IsRecoverySettlementActive());
	auto JournalStatus = [](bool Initialized, bool Healthy, bool RestorePending, int32 KnownRecords, int32 Writes, bool Sticky)
	{
		const auto Value = MakeShared<FJsonObject>();
		Value->SetBoolField(TEXT("initialized"), Initialized);
		Value->SetBoolField(TEXT("healthy"), Healthy);
		Value->SetBoolField(TEXT("restorePending"), RestorePending);
		Value->SetNumberField(TEXT("knownDurableRecords"), KnownRecords); // Cached knowledge, not a fresh disk assertion.
		Value->SetNumberField(TEXT("pendingWrites"), Writes);
		Value->SetBoolField(TEXT("unattributedFailure"), Sticky);
		return Value;
	};
	Root->SetObjectField(TEXT("consumeJournal"), JournalStatus(DungeonConsumeJournal.IsValid(),
		DungeonConsumeJournal && DungeonConsumeJournal->IsHealthy(), bDungeonConsumeRestorePending,
		DungeonConsumeJournal ? DungeonConsumeJournal->GetPending().Num() : 0,
		PendingDungeonConsumeObservationWrites.Num(), bDungeonConsumePersistenceFailed));
	const auto Outbox = JournalStatus(DungeonDisconnectionJournal.IsValid(),
		DungeonDisconnectionJournal && DungeonDisconnectionJournal->IsHealthy(), bDungeonDisconnectionRestorePending,
		DungeonDisconnectionJournal ? DungeonDisconnectionJournal->GetPending().Num() : 0,
		PendingDungeonDisconnectionWrites.Num(), bDungeonDisconnectionPersistenceFailed);
	Outbox->SetNumberField(TEXT("queuedReports"), DungeonDisconnectionReports.Num());
	Root->SetObjectField(TEXT("disconnectionOutbox"), Outbox);
	TMap<FString, TSharedPtr<FDungeonConsumeRecovery>> States;
	TSet<FString> ActiveIds;
	int32 ConflictingOwners = 0;
	auto Collect = [&States, &ConflictingOwners](const TSharedPtr<FDungeonConsumeRecovery>& State)
	{
		if (!State) return;
		if (const auto* Previous = States.Find(State->RequestId); Previous && *Previous != State) ++ConflictingOwners;
		else States.Add(State->RequestId, State);
	};
	for (const auto& Entry : DungeonConsumeRecoveries)
	{
		Collect(Entry.Value);
		if (ActiveDungeonConsumeRequests.Contains(Entry.Key)) ActiveIds.Add(Entry.Value->RequestId);
	}
	for (const auto& Entry : PendingDungeonConsumeObservationWrites) Collect(Entry.Value);
	if (DungeonConsumeJournal)
		for (const auto& Entry : DungeonConsumeJournal->GetPending())
			if (!States.Contains(Entry.Key)) States.Add(Entry.Key, nullptr);
	TArray<FString> Ids; States.GenerateKeyArray(Ids); Ids.Sort();
	Root->SetNumberField(TEXT("totalIntents"), Ids.Num());
	Root->SetNumberField(TEXT("conflictingOwners"), ConflictingOwners);
	TArray<TSharedPtr<FJsonValue>> Intents;
	const double Now = FPlatformTime::Seconds();
	const int32 Limit = FMath::Clamp(MaxIntents, 1, 64);
	for (const FString& Id : Ids)
	{
		if (Intents.Num() >= Limit) break;
		const auto State = States.FindRef(Id);
		const auto Value = MakeShared<FJsonObject>();
		Value->SetStringField(TEXT("consumptionRequestId"), SafeUuid(Id));
		Value->SetBoolField(TEXT("durableObservationKnown"), DungeonConsumeJournal && DungeonConsumeJournal->GetPending().Contains(Id));
		Value->SetBoolField(TEXT("observationWritePending"), PendingDungeonConsumeObservationWrites.Contains(Id));
		const FRPGDungeonConsumeLossRecord* Record = DungeonConsumeJournal ? DungeonConsumeJournal->GetPending().Find(Id) : nullptr;
		const FDateTime Observed = State ? State->LostAtUtc : Record ? Record->ObservedAtUtc : FDateTime();
		Value->SetStringField(TEXT("observedAtUtc"), Observed.GetTicks() > 0 ? Observed.ToIso8601() : FString());
		const TCHAR* Stage = TEXT("durable_record_unbound");
		const TCHAR* Outcome = TEXT("not_checked");
		bool Review = false;
		if (State)
		{
			const bool Current = !State->bRestoredWorldPending && IsCurrentDungeonMutationScope(State->Scope);
			Value->SetBoolField(TEXT("scopeCurrent"), Current);
			Value->SetBoolField(TEXT("expired"), State->bExpired);
			Value->SetBoolField(TEXT("uncertain"), State->bUncertain);
			Value->SetStringField(TEXT("knownConnectionGeneration"), State->EvidenceGeneration > 0 ? LexToString(State->EvidenceGeneration) : FString());
			Value->SetStringField(TEXT("lastOperation"), State->LastEvidenceOutcome == FDungeonConsumeRecovery::EEvidenceOutcome::NotChecked
				? TEXT("none") : State->bLastEvidenceWasSeal ? TEXT("seal") : TEXT("evidence"));
			Value->SetNumberField(TEXT("lastHttpStatus"), State->LastEvidenceHttpStatus);
			Value->SetStringField(TEXT("lastCompletedAtUtc"), State->LastEvidenceCompletedAtUtc.GetTicks() > 0
				? State->LastEvidenceCompletedAtUtc.ToIso8601() : FString());
			Value->SetNumberField(TEXT("retryAfterSeconds"), FMath::IsFinite(State->NextAttemptAt)
				? FMath::Clamp(State->NextAttemptAt - Now, 0.0, 86400.0) : 0.0);
			switch (State->LastEvidenceOutcome)
			{
			case FDungeonConsumeRecovery::EEvidenceOutcome::TransportFailure: Outcome = TEXT("transport_failure"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::HttpRejected: Outcome = TEXT("http_rejected"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::InvalidResponse: Outcome = TEXT("invalid_response"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::Unresolved: Outcome = TEXT("backend_unresolved"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::ConsumptionRecorded: Outcome = TEXT("consumption_recorded"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::CancellationVerified: Outcome = TEXT("cancellation_verified"); break;
			default: break;
			}
			Review = State->LastEvidenceOutcome == FDungeonConsumeRecovery::EEvidenceOutcome::Unresolved && State->bLastEvidenceWasSeal;
			if (PendingDungeonConsumeObservationWrites.Contains(Id)) Stage = TEXT("observation_write_pending");
			else if (State->bRestoredWorldPending) Stage = TEXT("awaiting_gameplay_world");
			else if (!Current) Stage = TEXT("scope_inactive");
			else if (State->bCancellationVerified) Stage = TEXT("cancellation_cleanup_pending");
			else if (State->EvidenceGeneration > 0) Stage = TEXT("disconnection_handoff_pending");
			else if (State->bExpired && State->LostAtUtc.GetTicks() <= 0) Stage = TEXT("loss_not_observed");
			else if (State->EvidenceRequest.IsValid()) Stage = State->bSealRequested ? TEXT("seal_in_flight") : TEXT("evidence_in_flight");
			else if (State->bExpired) Stage = State->bSealRequested ? TEXT("seal_retry_pending") : TEXT("evidence_retry_pending");
			else if (ActiveIds.Contains(Id)) Stage = TEXT("consume_in_flight");
			else Stage = TEXT("consume_retry_pending");
		}
		Value->SetStringField(TEXT("stage"), Stage);
		Value->SetStringField(TEXT("lastOutcome"), Outcome);
		Value->SetBoolField(TEXT("operatorReviewSuggested"), Review);
		Intents.Add(MakeShared<FJsonValueObject>(Value));
	}
	Root->SetArrayField(TEXT("intents"), Intents);
	Root->SetNumberField(TEXT("omittedIntents"), Ids.Num() - Intents.Num());
	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	return Json;
}

FHttpRequestPtr FRPGDungeonConnectionRecovery::CreateConsumeJoinTicketRequest(const FString& JoinTicket, const FString& ExpectedSteamId)
{
	if (!CanDispatchConfiguredDungeonConnectionMutation()
		|| ActiveDungeonConsumeRequests.Contains(JoinTicket)
		|| DungeonConsumeRecoveries.Contains(JoinTicket)
		|| JoinTicket.IsEmpty() || JoinTicket.Len() > 256
		|| !Host.HasRecoveryCredential()
		|| Host.GetRecoveryServerId().IsEmpty()
		|| Host.GetRecoverySessionId().IsEmpty())
	{
		return nullptr;
	}

	const auto Recovery = MakeShared<FDungeonConsumeRecovery>();
	Recovery->Scope = CaptureDungeonMutationScope();
	Recovery->RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Recovery->Deadline = FPlatformTime::Seconds() + 600.0;
	// Bind recovery to the original peer BEFORE any backend reply. A missing dev
	// identity may still use ordinary admission, but cannot resolve historical evidence.
	Recovery->ExpectedSteamId = ExpectedSteamId;
	// The consume endpoint hashes its trimmed token; the evidence key must match it.
	const FTCHARToUTF8 TicketUtf8(*JoinTicket.TrimStartAndEnd());
	uint8 Digest[SHA256_DIGEST_LENGTH];
	if (!SHA256(reinterpret_cast<const uint8*>(TicketUtf8.Get()), TicketUtf8.Length(), Digest)) return nullptr;
	Recovery->TicketHash = BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("joinTicket"), JoinTicket);
	RootObject->SetStringField(TEXT("serverId"), Recovery->Scope.ServerId);
	RootObject->SetStringField(TEXT("consumptionRequestId"), Recovery->RequestId);

	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&Recovery->Body);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		return nullptr;
	}
	DungeonConsumeRecoveries.Add(JoinTicket, Recovery);
	FHttpRequestPtr Request = CreateDungeonConsumeAttempt(JoinTicket, Recovery);
	if (!Request.IsValid()) DungeonConsumeRecoveries.Remove(JoinTicket);
	return Request;
}

FHttpRequestPtr FRPGDungeonConnectionRecovery::CreateDungeonConsumeAttempt(
	const FString& JoinTicket, const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (DungeonConsumeRecoveries.FindRef(JoinTicket) != Recovery
		|| Recovery->bExpired
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || !IsDungeonDisconnectionOutboxReady()
		|| ActiveDungeonConsumeRequests.Contains(JoinTicket) || !Host.HasRecoveryCredential()) return nullptr;
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	ActiveDungeonConsumeRequests.Add(JoinTicket, Request);
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetRecoveryOwner(),
		[this, Recovery, JoinTicket](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport)
		{
			if (!ClaimDungeonConsumeCompletion(ActiveDungeonConsumeRequests, JoinTicket, Completed)) return;
			if (DungeonConsumeRecoveries.FindRef(JoinTicket) != Recovery) return;
			// A newly queued loss blocks future sends, not an already committed response.
			OnConsumeJoinTicketResponseReceived(Completed, Response, bTransport, JoinTicket);
		});
	Request->SetURL(Recovery->Scope.BackendUrl / TEXT("join-tickets/consume"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Host.ApplyRecoveryHeaders(Request);
	Request->SetContentAsString(Recovery->Body);
	Request->SetTimeout(Host.GetRecoveryRequestTimeout());
	return Request;
}

void FRPGDungeonConnectionRecovery::ObserveDungeonConsumeConnectionLoss(const FString& JoinTicket,
	const FString& ExpectedSteamId, FDateTime ObservedAtUtc)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(JoinTicket);
	if (!Recovery || !IsCurrentDungeonMutationScope(Recovery->Scope) || ObservedAtUtc.GetTicks() <= 0
		|| Recovery->ExpectedSteamId.IsEmpty()
		|| !Recovery->ExpectedSteamId.Equals(ExpectedSteamId, ESearchCase::CaseSensitive)) return;
	if (Recovery->LostAtUtc.GetTicks() == 0) Recovery->LostAtUtc = ObservedAtUtc;
	PersistDungeonConsumeObservation(Recovery.ToSharedRef());
}

void FRPGDungeonConnectionRecovery::PersistDungeonConsumeObservation(const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (!IsCurrentDungeonMutationScope(Recovery->Scope)) return;
	const FRPGDungeonConsumeLossRecord Record {Recovery->RequestId, Recovery->TicketHash,
		Recovery->ExpectedSteamId, Recovery->LostAtUtc};
	if (const auto Existing = PendingDungeonConsumeObservationWrites.FindRef(Recovery->RequestId); Existing && Existing != Recovery)
	{
		// Never overwrite an earlier failed intent under a colliding UUID.
		bDungeonConsumePersistenceFailed = true;
		return;
	}
	if (!bDungeonConsumeRestorePending && DungeonConsumeJournal && DungeonConsumeJournal->MatchesAssignment(
		Recovery->Scope.BackendUrl, Recovery->Scope.ServerId, Recovery->Scope.SessionId) && DungeonConsumeJournal->Persist(Record))
	{
		PendingDungeonConsumeObservationWrites.Remove(Recovery->RequestId);
		return;
	}
	if (PendingDungeonConsumeObservationWrites.IsEmpty())
	{
		DungeonConsumeObservationWriteRetries = 0;
		NextDungeonConsumeObservationWriteAt = FPlatformTime::Seconds() + 1.0;
	}
	// The original first UTC/peer/hash live on this exact intent even if an old
	// HTTP callback is later cancelled. Never log/persist its in-memory ticket body.
	PendingDungeonConsumeObservationWrites.Add(Recovery->RequestId, Recovery);
}

void FRPGDungeonConnectionRecovery::RetryDungeonConsumeObservationWrites(double Now)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown || bDungeonConsumeRecoveryCompleting || bDungeonConsumeRestorePending
		|| PendingDungeonConsumeObservationWrites.IsEmpty() || Now < NextDungeonConsumeObservationWriteAt) return;
	NextDungeonConsumeObservationWriteAt = Now + (DungeonConsumeObservationWriteRetries++ == 0 ? 3.0 : 30.0);
	TGuardValue<bool> WriteGuard(bDungeonConsumeRecoveryCompleting, true);
	if (!DungeonConsumeJournal) return;
	TArray<FString> Keys; PendingDungeonConsumeObservationWrites.GenerateKeyArray(Keys);
	int32 Attempts = 0;
	bool bStorageChecked = false;
	// Rotate bounded batches so repeatedly failing records cannot starve other
	// original observations queued before the mutation barrier closed.
	const int32 Offset = static_cast<int32>((static_cast<int64>(DungeonConsumeObservationWriteRetries - 1) * 4) % Keys.Num());
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FString& Key = Keys[(Offset + Index) % Keys.Num()];
		const auto Recovery = PendingDungeonConsumeObservationWrites.FindRef(Key);
		if (!Recovery || Recovery->bRestoredWorldPending || !IsCurrentDungeonMutationScope(Recovery->Scope)
			|| !DungeonConsumeJournal->MatchesAssignment(Recovery->Scope.BackendUrl, Recovery->Scope.ServerId, Recovery->Scope.SessionId)) continue;
		if (!bStorageChecked)
		{
			// Revalidate the entire directory and all previously durable entries.
			// Partial/corrupt/conflicting files are retained, never "repaired" by deletion.
			if (!DungeonConsumeJournal->IsHealthy() && !DungeonConsumeJournal->RecoverForRetry()) return;
			bStorageChecked = true;
		}
		PersistDungeonConsumeObservation(Recovery.ToSharedRef());
		if (++Attempts >= 4) break;
	}
}

bool FRPGDungeonConnectionRecovery::HasExpiredDungeonConsumeRecovery(const FString& JoinTicket) const
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(JoinTicket);
	return Recovery && Recovery->bExpired && IsCurrentDungeonMutationScope(Recovery->Scope);
}

void FRPGDungeonConnectionRecovery::ExpireDungeonConsumeRecovery(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (Recovery->bExpired || DungeonConsumeRecoveries.FindRef(Ticket) != Recovery) return;
	Recovery->bExpired = true;
	Recovery->bUncertain = true;
	Recovery->bWaiting = false;
	Recovery->NextAttemptAt = 0.0;
	UE_LOG(LogTemp, Warning, TEXT("Dungeon admission recovery expired; retaining the original intent for evidence reconciliation."));
	// Exactly one engine failure; historical evidence must NEVER broadcast a success.
	Host.BroadcastConsumedTicket(Ticket, FString(), FString(), FString(), 0, false);
}

FHttpRequestPtr FRPGDungeonConnectionRecovery::TakeDueDungeonConsumeRetry(const FString& Ticket, double Now)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(Ticket);
	if (!Recovery || !Recovery->bWaiting || ActiveDungeonConsumeRequests.Contains(Ticket)) return nullptr;
	if (!IsCurrentDungeonMutationScope(Recovery->Scope))
	{
		DungeonConsumeRecoveries.Remove(Ticket);
		return nullptr;
	}
	if (Now >= Recovery->Deadline)
	{
		ExpireDungeonConsumeRecovery(Ticket, Recovery.ToSharedRef());
		return nullptr;
	}
	if (Now < Recovery->NextAttemptAt) return nullptr;
	FHttpRequestPtr Request = CreateDungeonConsumeAttempt(Ticket, Recovery.ToSharedRef());
	if (Request.IsValid()) Recovery->bWaiting = false;
	else Recovery->NextAttemptAt = Now + 1.0;
	return Request;
}

FHttpRequestPtr FRPGDungeonConnectionRecovery::TakeDueDungeonConsumeEvidence(const FString& Ticket, double Now)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(Ticket);
	if (bDungeonConsumeRestorePending || !Recovery || !Recovery->bExpired || Recovery->bRestoredWorldPending || Recovery->EvidenceRequest.IsValid()
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || Now < Recovery->NextAttemptAt
		|| Recovery->LostAtUtc.GetTicks() <= 0 || !Host.HasRecoveryCredential()) return nullptr;
	if (Recovery->bCancellationVerified)
	{
		ResolveCancelledDungeonConsumeRecovery(Ticket, Recovery.ToSharedRef());
		return nullptr;
	}
	if (Recovery->EvidenceGeneration > 0)
	{
		QueueDungeonConsumeEvidence(Ticket, Recovery.ToSharedRef());
		return nullptr;
	}
	if (Recovery->ExpectedSteamId.IsEmpty() || Recovery->ExpectedSteamId.Len() > 20) return nullptr;
	for (const TCHAR Character : Recovery->ExpectedSteamId)
		if (Character < TEXT('0') || Character > TEXT('9')) return nullptr;
	if (Recovery->EvidenceBody.IsEmpty())
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("dungeonSessionId"), Recovery->Scope.SessionId);
		Body->SetStringField(TEXT("serverId"), Recovery->Scope.ServerId);
		Body->SetStringField(TEXT("consumptionRequestId"), Recovery->RequestId);
		Body->SetStringField(TEXT("ticketHash"), Recovery->TicketHash);
		Body->SetStringField(TEXT("expectedSteamId"), Recovery->ExpectedSteamId);
		if (!FJsonSerializer::Serialize(Body,
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Recovery->EvidenceBody))) return nullptr;
	}
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Recovery->EvidenceRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetRecoveryOwner(),
		[this, Ticket, Recovery](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport)
		{ CompleteDungeonConsumeEvidence(Ticket, Recovery.ToSharedRef(), Completed, Response, bTransport); });
	Request->SetURL(Recovery->Scope.BackendUrl / (Recovery->bSealRequested
		? TEXT("join-tickets/seal-consumption") : TEXT("join-tickets/consumption-evidence")));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Host.ApplyRecoveryHeaders(Request);
	Request->SetContentAsString(Recovery->EvidenceBody);
	Request->SetTimeout(Host.GetRecoveryRequestTimeout());
	return Request;
}

void FRPGDungeonConnectionRecovery::DispatchDungeonConsumeEvidence(const FString& Ticket, const FHttpRequestPtr& Request)
{
	if (!Request.IsValid() || Request->ProcessRequest()) return;
	Request->OnProcessRequestComplete().Unbind();
	const auto Recovery = DungeonConsumeRecoveries.FindRef(Ticket);
	if (Recovery) CompleteDungeonConsumeEvidence(Ticket, Recovery.ToSharedRef(), Request, nullptr, false);
}

void FRPGDungeonConnectionRecovery::CompleteDungeonConsumeEvidence(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery, const FHttpRequestPtr& Request,
	const FHttpResponsePtr& Response, bool bTransport)
{
	if (DungeonConsumeRecoveries.FindRef(Ticket) != Recovery
		|| !Recovery->EvidenceRequest.IsValid() || Recovery->EvidenceRequest != Request) return;
	Recovery->EvidenceRequest.Reset(); // Claim exactly once, including synchronous ProcessRequest(false).
	if (!IsCurrentDungeonMutationScope(Recovery->Scope)) return;
	Recovery->NextAttemptAt = FPlatformTime::Seconds() + 30.0;
	const bool bSeal = Request->GetURL().Equals(Recovery->Scope.BackendUrl / TEXT("join-tickets/seal-consumption"), ESearchCase::CaseSensitive);
	Recovery->bLastEvidenceWasSeal = bSeal;
	Recovery->LastEvidenceCompletedAtUtc = FDateTime::UtcNow(); // Not an observed socket-loss timestamp.
	Recovery->LastEvidenceHttpStatus = Response.IsValid() ? Response->GetResponseCode() : 0;
	Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::InvalidResponse;
	if (!bTransport || !Response.IsValid())
	{
		Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::TransportFailure;
		return;
	}
	if (Response->GetContentLength() > 8192) return;
	if (Response->GetResponseCode() == 409)
	{
		TSharedPtr<FJsonObject> Error;
		FString Code;
		// Only the authenticated endpoint's exact marker requests sealing. A
		// generic conflict, missing record or HTTP denial never releases a guard.
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Error)
			&& Error && Error->Values.Num() == 1 && Error->TryGetStringField(TEXT("error"), Code)
			&& Code.Equals(TEXT("join_ticket_consumption_unresolved"), ESearchCase::CaseSensitive))
		{
			Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::Unresolved;
			if (!bSeal) Recovery->bSealRequested = true;
		}
		return;
	}
	if (Response->GetResponseCode() != 200)
	{
		Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::HttpRejected;
		return;
	}
	FRPGJoinTicketConsumptionCancellation Cancellation;
	if (bSeal && Recovery->EvidenceGeneration == 0
		&& FRPGJoinTicketConsumptionCancellation::Decode(Response->GetContentAsString(), Cancellation)
		&& Cancellation.DungeonSessionId.Equals(Recovery->Scope.SessionId, ESearchCase::CaseSensitive)
		&& Cancellation.ServerId.Equals(Recovery->Scope.ServerId, ESearchCase::CaseSensitive)
		&& Cancellation.ConsumptionRequestId.Equals(Recovery->RequestId, ESearchCase::CaseSensitive)
		&& Cancellation.TicketHash.Equals(Recovery->TicketHash, ESearchCase::CaseSensitive)
		&& Cancellation.ExpectedSteamId.Equals(Recovery->ExpectedSteamId, ESearchCase::CaseSensitive))
	{
		Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::CancellationVerified;
		Recovery->bCancellationVerified = true;
		ResolveCancelledDungeonConsumeRecovery(Ticket, Recovery);
		return;
	}
	FRPGJoinTicketConsumptionEvidence Evidence;
	if (!FRPGJoinTicketConsumptionEvidence::Decode(Response->GetContentAsString(), Evidence)
		|| !Evidence.Consumption.DungeonSessionId.Equals(Recovery->Scope.SessionId, ESearchCase::CaseSensitive)
		|| !Evidence.ServerId.Equals(Recovery->Scope.ServerId, ESearchCase::CaseSensitive)
		|| !Evidence.ConsumptionRequestId.Equals(Recovery->RequestId, ESearchCase::CaseSensitive)
		|| !Evidence.Consumption.SteamId.Equals(Recovery->ExpectedSteamId, ESearchCase::CaseSensitive)) return;
	Recovery->EvidenceCharacterId = Evidence.Consumption.CharacterId;
	Recovery->EvidenceGeneration = Evidence.Consumption.ConnectionGeneration;
	Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::ConsumptionRecorded;
	QueueDungeonConsumeEvidence(Ticket, Recovery);
}

bool FRPGDungeonConnectionRecovery::QueueDungeonConsumeEvidence(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (DungeonConsumeRecoveries.FindRef(Ticket) != Recovery || !Recovery->bExpired
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || Recovery->EvidenceGeneration <= 0
		|| Recovery->LostAtUtc.GetTicks() <= 0 || bDungeonConsumeRecoveryCompleting) return false;
	TGuardValue<bool> HandoffGuard(bDungeonConsumeRecoveryCompleting, true);
	Recovery->NextAttemptAt = FPlatformTime::Seconds() + 30.0;
	if (!ReportConfiguredDungeonMemberDisconnection(Recovery->Scope.SessionId, Recovery->EvidenceCharacterId,
		Recovery->ExpectedSteamId, Recovery->EvidenceGeneration, Recovery->LostAtUtc)) return false;
	if (!DungeonConsumeJournal || !DungeonConsumeJournal->Resolve(Recovery->RequestId)) return false;
	// Only durable ownership transfer removes THIS uncertainty. Other intents and
	// the outbox/persistence-failure barrier remain authoritative.
	DungeonConsumeRecoveries.Remove(Ticket);
	return true;
}

bool FRPGDungeonConnectionRecovery::ResolveCancelledDungeonConsumeRecovery(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (DungeonConsumeRecoveries.FindRef(Ticket) != Recovery || !Recovery->bExpired
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !Recovery->bCancellationVerified || Recovery->EvidenceGeneration != 0
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || bDungeonConsumeRecoveryCompleting) return false;
	TGuardValue<bool> CompletionGuard(bDungeonConsumeRecoveryCompleting, true);
	Recovery->NextAttemptAt = FPlatformTime::Seconds() + 30.0;
	// The backend proof fences late consumes, but does not delete our durable
	// observation. Keep the intent if removal fails; no generation/disconnect is invented.
	if (!DungeonConsumeJournal || !DungeonConsumeJournal->Resolve(Recovery->RequestId)) return false;
	DungeonConsumeRecoveries.Remove(Ticket);
	return true;
}

void FRPGDungeonConnectionRecovery::InitializeDungeonConsumeRecoveries(const FString& Directory)
{
	DungeonConsumeJournal = MakeShared<FRPGDungeonConsumeJournal>(Directory, Host.GetRecoveryApiUrl(), Host.GetRecoveryServerId(), Host.GetRecoverySessionId());
	bDungeonConsumeRestorePending = true;
	if (!RestoreDungeonConsumeRecoveries())
	{
		UE_LOG(LogTemp, Warning, TEXT("Dungeon pending-consumption loss journal could not be restored; files retained, retry scheduled and new mutations blocked."));
	}
	NextDungeonJournalRestoreAt = FPlatformTime::Seconds() + 1.0;
}

bool FRPGDungeonConnectionRecovery::RestoreDungeonConsumeRecoveries()
{
	if (!DungeonConsumeJournal || !DungeonConsumeJournal->RecoverForRetry()) return false;
	// Validate every collision before publishing anything. Preserve any live intent,
	// request/delegate ownership and its first loss time instead of overwriting it.
	for (const auto& Entry : DungeonConsumeJournal->GetPending())
		for (const auto& Existing : DungeonConsumeRecoveries)
		{
			const auto& State = Existing.Value;
			if (State->RequestId != Entry.Key) continue;
			const FRPGDungeonConsumeLossRecord Known {State->RequestId, State->TicketHash, State->ExpectedSteamId, State->LostAtUtc};
			if (!Known.SameAs(Entry.Value) || !DungeonConsumeJournal->MatchesAssignment(
				State->Scope.BackendUrl, State->Scope.ServerId, State->Scope.SessionId))
			{
				bDungeonConsumePersistenceFailed = true;
				return false;
			}
		}
	for (const auto& Entry : DungeonConsumeJournal->GetPending())
	{
		bool bAlreadyOwned = false;
		for (const auto& Existing : DungeonConsumeRecoveries)
			if (Existing.Value->RequestId == Entry.Key) { bAlreadyOwned = true; break; }
		if (bAlreadyOwned) continue;
		const auto Recovery = MakeShared<FDungeonConsumeRecovery>();
		Recovery->Scope = CaptureDungeonMutationScope();
		Recovery->RequestId = Entry.Value.RequestId;
		Recovery->TicketHash = Entry.Value.TicketHash;
		Recovery->ExpectedSteamId = Entry.Value.ExpectedSteamId;
		Recovery->LostAtUtc = Entry.Value.ObservedAtUtc;
		Recovery->bExpired = true;
		Recovery->bUncertain = true;
		Recovery->bRestoredWorldPending = true;
		// No original socket/delegate or ticket survives: reconcile proof only, never re-consume/admit.
		DungeonConsumeRecoveries.Add(TEXT("restored:") + Entry.Key, Recovery);
	}
	bDungeonConsumeRestorePending = false;
	return true;
}

void FRPGDungeonConnectionRecovery::BindRestoredDungeonConsumeWorld()
{
	UWorld* World = Host.GetRecoveryGameInstance() ? Host.GetRecoveryGameInstance()->GetWorld() : nullptr;
	if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| !World->GetBegunPlay()
		|| World->GetGameInstance() != Host.GetRecoveryGameInstance() || World->GetNetMode() == NM_Client
		|| bDungeonDisconnectionReportShutdown) return;
	for (auto& Entry : DungeonConsumeRecoveries)
	{
		auto& State = Entry.Value;
		if (!State->bRestoredWorldPending || !State->Scope.BackendUrl.Equals(Host.GetRecoveryApiUrl(), ESearchCase::CaseSensitive)
			|| !State->Scope.ServerId.Equals(Host.GetRecoveryServerId(), ESearchCase::CaseSensitive)
			|| !State->Scope.SessionId.Equals(Host.GetRecoverySessionId(), ESearchCase::CaseSensitive)) continue;
		// Initialization creates a Game-typed dummy world. Adopt the first begun-play world
		// ONCE, never migrate a callback/intent after it has been bound.
		State->Scope.World = World;
		State->bRestoredWorldPending = false;
	}
}

void FRPGDungeonConnectionRecovery::TickDungeonConsumeRecoveries()
{
	if (bDungeonConsumeRecoveryCompleting) return; // Report enqueue can synchronously tick this subsystem.
	BindRestoredDungeonConsumeWorld();
	RetryDungeonConsumeObservationWrites(FPlatformTime::Seconds());
	TArray<FString> Tickets;
	DungeonConsumeRecoveries.GenerateKeyArray(Tickets);
	int32 Started = 0;
	for (const FString& Ticket : Tickets)
	{
		FHttpRequestPtr Request = TakeDueDungeonConsumeRetry(Ticket, FPlatformTime::Seconds());
		if (Request.IsValid()) DispatchDungeonConsumeAttempt(Ticket, Request);
		else
		{
			Request = TakeDueDungeonConsumeEvidence(Ticket, FPlatformTime::Seconds());
			if (!Request.IsValid()) continue;
			DispatchDungeonConsumeEvidence(Ticket, Request);
		}
		if (++Started >= 4) break;
	}
}

bool FRPGDungeonConnectionRecovery::ConfirmConfiguredDungeonMemberAdmission(
	const FString& DungeonSessionId, const FString& CharacterId,
	const FString& SteamId, int64 ConnectionGeneration,
	TFunction<void(bool, int32)> Completion)
{
	FString Body;
	if (!Host.IsRecoveryServerRuntime() || !Host.IsRecoveryServerConfigured()
		|| DungeonSessionId != Host.GetRecoverySessionId()
		|| !FRPGDungeonConnectionRequest::Encode(DungeonSessionId, CharacterId,
			SteamId, ConnectionGeneration, Body)
		|| !Host.HasValidRecoveryServiceCredential()) return false;

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetRecoveryOwner(),
		[Completion = MoveTemp(Completion)](FHttpRequestPtr, FHttpResponsePtr Response,
			bool bTransportSucceeded) mutable
		{
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			bool bConfirmed = false;
			if (bTransportSucceeded && Status == 200 && Response.IsValid()
				&& Response->GetContentLength() <= 4096)
				bConfirmed = FRPGDungeonAdmissionConfirmation::Decode(
					Response->GetContentAsString(), Status);
			if (Completion) Completion(bConfirmed, Status);
		});
	Request->SetURL(Host.GetRecoveryApiUrl() / FString::Printf(
		TEXT("dungeon-sessions/%s/members/%s/admission-confirmation"),
		*DungeonSessionId, *CharacterId));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Host.ApplyRecoveryServiceAuthorization(Request);
	Request->SetContentAsString(Body);
	Request->SetTimeout(FMath::IsFinite(Host.GetRecoveryRequestTimeout())
		? FMath::Clamp(Host.GetRecoveryRequestTimeout(), 1.f, 30.f) : 10.f);
	return Request->ProcessRequest();
}

void FRPGDungeonConnectionRecovery::InitializeDungeonDisconnectionReports()
{
	// Validate the session before using it as a path component.
	FGuid Session;
	if (!FGuid::Parse(Host.GetRecoverySessionId(), Session) || !Session.IsValid()
		|| !Host.GetRecoverySessionId().Equals(Session.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)) return;
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Online/DungeonDisconnections"),
		FMD5::HashAnsiString(*Host.GetRecoveryApiUrl()), FMD5::HashAnsiString(*Host.GetRecoveryServerId()), Host.GetRecoverySessionId());
	InitializeDungeonDisconnectionReports(Directory);
}

void FRPGDungeonConnectionRecovery::InitializeDungeonDisconnectionReports(const FString& Directory)
{
	// Initialize once per assigned manager, including when the first read fails.
	if (DungeonDisconnectionJournal || bDungeonDisconnectionReportShutdown) return;
	DungeonDisconnectionJournal = MakeShared<FRPGDungeonDisconnectionJournal>(
		Directory, Host.GetRecoveryApiUrl(), Host.GetRecoveryServerId(), Host.GetRecoverySessionId());
	bDungeonDisconnectionRestorePending = true;
	if (!RestoreDungeonDisconnectionReports())
	{
		UE_LOG(LogTemp, Warning, TEXT("Dungeon connection loss journal could not be restored; files retained and retry scheduled."));
	}
	InitializeDungeonConsumeRecoveries(FPaths::Combine(Directory, TEXT("ConsumeRecoveries")));
	DungeonJournalRestoreRetries = 0;
	// A failed parent restore must not prevent its own recovery ticker from starting.
	DungeonDisconnectionTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(&Host.GetRecoveryOwner(), [this](float DeltaTime) { return TickDungeonDisconnectionReports(DeltaTime); }), 1.f);
}

bool FRPGDungeonConnectionRecovery::RestoreDungeonDisconnectionReports()
{
	if (!DungeonDisconnectionJournal || !DungeonDisconnectionJournal->RecoverForRetry()) return false;
	for (const auto& Entry : DungeonDisconnectionJournal->GetPending())
		if (const auto Existing = DungeonDisconnectionReports.FindRef(Entry.Key))
			if (Existing->Body != Entry.Value.Body || Existing->SteamId != Entry.Value.SteamId)
			{
				bDungeonDisconnectionPersistenceFailed = true;
				return false;
			}
	for (const auto& Entry : DungeonDisconnectionJournal->GetPending())
	{
		if (DungeonDisconnectionReports.Contains(Entry.Key)) continue;
		auto Report = MakeShared<FDungeonDisconnectionReport>();
		Report->DungeonSessionId = Host.GetRecoverySessionId();
		Report->CharacterId = Entry.Value.CharacterId;
		Report->SteamId = Entry.Value.SteamId;
		Report->ConnectionGeneration = Entry.Value.ConnectionGeneration;
		Report->ObservedAtUtc = Entry.Value.ObservedAtUtc;
		Report->Body = Entry.Value.Body;
		DungeonDisconnectionReports.Add(Entry.Key, MoveTemp(Report));
	}
	bDungeonDisconnectionRestorePending = false;
	if (!DungeonDisconnectionReports.IsEmpty())
		UE_LOG(LogTemp, Display, TEXT("Dungeon connection loss journal restored %d pending reports."),
			DungeonDisconnectionReports.Num());
	return true;
}

void FRPGDungeonConnectionRecovery::RetryDungeonJournalRestores(double Now)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown || bDungeonConsumeRecoveryCompleting
		|| (!bDungeonDisconnectionRestorePending && !bDungeonConsumeRestorePending) || Now < NextDungeonJournalRestoreAt
		|| !DungeonDisconnectionJournal || !DungeonConsumeJournal
		|| !DungeonDisconnectionJournal->MatchesAssignment(Host.GetRecoveryApiUrl(), Host.GetRecoveryServerId(), Host.GetRecoverySessionId())
		|| !DungeonConsumeJournal->MatchesAssignment(Host.GetRecoveryApiUrl(), Host.GetRecoveryServerId(), Host.GetRecoverySessionId())) return;
	NextDungeonJournalRestoreAt = Now + (DungeonJournalRestoreRetries++ == 0 ? 3.0 : 30.0);
	TGuardValue<bool> RestoreGuard(bDungeonConsumeRecoveryCompleting, true);
	// Loaded journals retain their in-flight objects and ACK/removal state. An
	// independent failed child must not rehydrate or reset a healthy parent.
	if (bDungeonDisconnectionRestorePending) RestoreDungeonDisconnectionReports();
	if (bDungeonConsumeRestorePending) RestoreDungeonConsumeRecoveries();
}

bool FRPGDungeonConnectionRecovery::TickDungeonDisconnectionReports(float)
{
	if (bDungeonDisconnectionReportShutdown) return false;
	// Local restoration needs no socket, credential or begun-play world. Restored
	// consume evidence still binds to the first real world before any HTTP work.
	RetryDungeonJournalRestores(FPlatformTime::Seconds());
	UWorld* World = Host.GetRecoveryGameInstance() ? Host.GetRecoveryGameInstance()->GetWorld() : nullptr;
	if (!IsValid(World) || World->bIsTearingDown || !World->IsGameWorld()
		|| World->GetGameInstance() != Host.GetRecoveryGameInstance()) return true;
	RetryDungeonDisconnectionWrites(FPlatformTime::Seconds());
	TArray<TSharedPtr<FDungeonDisconnectionReport>> Reports;
	DungeonDisconnectionReports.GenerateValueArray(Reports);
	const double Now = FPlatformTime::Seconds();
	int32 Started = 0;
	for (const auto& Report : Reports)
	{
		if (Report->Request.IsValid() || Now < Report->NextAttemptAt) continue;
		if (!SendDungeonDisconnectionAttempt(Report.ToSharedRef())) Report->NextAttemptAt = Now + 30.0;
		if (++Started >= 4) break;
	}
	TickDungeonConsumeRecoveries();
	return true;
}

bool FRPGDungeonConnectionRecovery::ReportConfiguredDungeonMemberDisconnection(
	const FString& DungeonSessionId, const FString& CharacterId,
	const FString& SteamId, int64 ConnectionGeneration, FDateTime ObservedAtUtc)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown) return false;
	FString Body;
	// Local custody must survive a temporarily unavailable service credential.
	// IsConfiguredForDungeonServer also requires that token; HTTP still uses it below.
	if (!Host.IsRecoveryServerRuntime() || Host.GetRecoveryServerId().IsEmpty() || Host.GetRecoverySessionId().IsEmpty()
		|| !DungeonDisconnectionJournal || !DungeonDisconnectionJournal->MatchesAssignment(Host.GetRecoveryApiUrl(), Host.GetRecoveryServerId(), DungeonSessionId)
		|| DungeonSessionId != Host.GetRecoverySessionId()
		|| !FRPGDungeonConnectionRequest::Encode(DungeonSessionId, CharacterId,
			SteamId, ConnectionGeneration, Body))
		return false;
	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = CharacterId;
	Record.SteamId = SteamId;
	Record.ConnectionGeneration = ConnectionGeneration;
	const FString Key = Record.Key();
	if (const auto* PendingWrite = PendingDungeonDisconnectionWrites.Find(Key))
	{
		if (!PendingWrite->Report || !PendingWrite->Report->SteamId.Equals(SteamId, ESearchCase::CaseSensitive)
			|| !IsCurrentDungeonMutationScope(PendingWrite->Scope)) return false;
		// A retry never replaces the original first loss, even before it reached disk.
		const bool bSaved = PersistDungeonDisconnectionWrite(Key, *PendingWrite);
		if (bSaved && !bDungeonConsumeRecoveryCompleting) TickDungeonDisconnectionReports(0.f);
		return bSaved;
	}
	if (const auto* Existing = DungeonDisconnectionJournal->GetPending().Find(Key))
	{
		if (!Existing->SteamId.Equals(SteamId, ESearchCase::CaseSensitive)) return false;
		// A duplicate cleanup must not replace the original timestamp, including v1.
		if (DungeonDisconnectionJournal->IsHealthy() && DungeonDisconnectionReports.Contains(Key)) return true;
		Record = *Existing;
	}
	else
	{
		if (ObservedAtUtc.GetTicks() <= 0) return false;
		Record.ObservedAtUtc = ObservedAtUtc;
		if (!Record.EncodeBody(DungeonSessionId, Record.Body)) return false;
	}
	const TSharedRef<FDungeonDisconnectionReport> Report =
		MakeShared<FDungeonDisconnectionReport>();
	Report->DungeonSessionId = DungeonSessionId;
	Report->CharacterId = CharacterId;
	Report->SteamId = SteamId;
	Report->ConnectionGeneration = ConnectionGeneration;
	Report->ObservedAtUtc = Record.ObservedAtUtc;
	Report->Body = Record.Body;
	if (const auto Existing = DungeonDisconnectionReports.FindRef(Key))
	{
		if (Existing->Body != Report->Body || Existing->SteamId != Report->SteamId)
		{
			bDungeonDisconnectionPersistenceFailed = true;
			return false;
		}
	}
	if (PendingDungeonDisconnectionWrites.IsEmpty())
	{
		DungeonDisconnectionWriteRetries = 0;
		NextDungeonDisconnectionWriteAt = FPlatformTime::Seconds() + 1.0;
	}
	FDungeonDisconnectionWrite Write {CaptureDungeonMutationScope(), Report};
	PendingDungeonDisconnectionWrites.Add(Key, Write);
	if (!PersistDungeonDisconnectionWrite(Key, Write)) return false;
	// The ticker also resumes this entry after a world change or process restart.
	if (!bDungeonConsumeRecoveryCompleting) TickDungeonDisconnectionReports(0.f);
	return true;
}

bool FRPGDungeonConnectionRecovery::PersistDungeonDisconnectionWrite(const FString& Key, const FDungeonDisconnectionWrite& Write)
{
	if (bDungeonDisconnectionRestorePending || !Write.Report || !IsCurrentDungeonMutationScope(Write.Scope) || !DungeonDisconnectionJournal
		|| !DungeonDisconnectionJournal->MatchesAssignment(Write.Scope.BackendUrl, Write.Scope.ServerId, Write.Scope.SessionId)) return false;
	const auto Report = Write.Report; // Own it before removing the map entry below.
	const FRPGDungeonDisconnectionRecord Record {Report->CharacterId, Report->SteamId,
		Report->ConnectionGeneration, Report->ObservedAtUtc, Report->Body};
	if (!DungeonDisconnectionJournal->Persist(Record)) return false;
	if (const auto Existing = DungeonDisconnectionReports.FindRef(Key))
	{
		if (Existing->Body != Report->Body || Existing->SteamId != Report->SteamId)
		{
			bDungeonDisconnectionPersistenceFailed = true;
			return false;
		}
	}
	else DungeonDisconnectionReports.Add(Key, Report);
	PendingDungeonDisconnectionWrites.Remove(Key);
	return true;
}

void FRPGDungeonConnectionRecovery::RetryDungeonDisconnectionWrites(double Now)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown || bDungeonConsumeRecoveryCompleting || bDungeonDisconnectionRestorePending
		|| PendingDungeonDisconnectionWrites.IsEmpty() || Now < NextDungeonDisconnectionWriteAt) return;
	NextDungeonDisconnectionWriteAt = Now + (DungeonDisconnectionWriteRetries++ == 0 ? 3.0 : 30.0);
	TGuardValue<bool> WriteGuard(bDungeonConsumeRecoveryCompleting, true);
	if (!DungeonDisconnectionJournal) return;
	TArray<FString> Keys; PendingDungeonDisconnectionWrites.GenerateKeyArray(Keys);
	const int32 Offset = static_cast<int32>((static_cast<int64>(DungeonDisconnectionWriteRetries - 1) * 4) % Keys.Num());
	int32 Attempts = 0;
	bool bStorageChecked = false;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FString Key = Keys[(Offset + Index) % Keys.Num()];
		const auto* Write = PendingDungeonDisconnectionWrites.Find(Key);
		if (!Write || !IsCurrentDungeonMutationScope(Write->Scope)
			|| !DungeonDisconnectionJournal->MatchesAssignment(Write->Scope.BackendUrl, Write->Scope.ServerId, Write->Scope.SessionId)) continue;
		if (!bStorageChecked)
		{
			if (!DungeonDisconnectionJournal->IsHealthy() && !DungeonDisconnectionJournal->RecoverForRetry()) return;
			bStorageChecked = true;
		}
		PersistDungeonDisconnectionWrite(Key, *Write);
		if (++Attempts >= 4) break;
	}
}

bool FRPGDungeonConnectionRecovery::ScheduleDungeonDisconnectionRetry(
	const TSharedRef<FDungeonDisconnectionReport>& Report,
	int32 CompletedAttempt, int32 HttpStatus)
{
	if (bDungeonDisconnectionReportShutdown || Report->Attempts != CompletedAttempt) return false;
	float Delay = FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(
		CompletedAttempt, HttpStatus);
	if (Delay < 0.f) Delay = 30.f;
	Report->NextAttemptAt = FPlatformTime::Seconds() + Delay;
	return true;
}

bool FRPGDungeonConnectionRecovery::SendDungeonDisconnectionAttempt(
	const TSharedRef<FDungeonDisconnectionReport>& Report)
{
	FHttpRequestPtr Request = CreateDungeonDisconnectionAttempt(Report);
	if (!Request.IsValid()) return false;
	if (Request->ProcessRequest()) return true;
	Request->OnProcessRequestComplete().Unbind();
	Report->Request.Reset();
	return ScheduleDungeonDisconnectionRetry(Report, Report->Attempts, 0);
}

FHttpRequestPtr FRPGDungeonConnectionRecovery::CreateDungeonDisconnectionAttempt(
	const TSharedRef<FDungeonDisconnectionReport>& Report)
{
	FString Body;
	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = Report->CharacterId;
	Record.SteamId = Report->SteamId;
	Record.ConnectionGeneration = Report->ConnectionGeneration;
	Record.ObservedAtUtc = Report->ObservedAtUtc;
	const FString Key = Record.Key();
	const auto* Durable = DungeonDisconnectionJournal ? DungeonDisconnectionJournal->GetPending().Find(Key) : nullptr;
	if (bDungeonDisconnectionReportShutdown || bDungeonDisconnectionRestorePending || !Host.IsRecoveryServerRuntime()
		|| !Host.IsRecoveryServerConfigured()
		|| !DungeonDisconnectionJournal || !DungeonDisconnectionJournal->IsHealthy()
		|| !DungeonDisconnectionJournal->MatchesAssignment(Host.GetRecoveryApiUrl(), Host.GetRecoveryServerId(), Report->DungeonSessionId)
		|| PendingDungeonDisconnectionWrites.Contains(Key) || !Durable || Durable->Body != Report->Body || Durable->SteamId != Report->SteamId
		|| Report->Request.IsValid()
		|| Report->DungeonSessionId != Host.GetRecoverySessionId()
		|| !Record.EncodeBody(Report->DungeonSessionId, Body)
		|| !Body.Equals(Report->Body, ESearchCase::CaseSensitive)
		|| !Host.HasValidRecoveryServiceCredential())
		return nullptr;
	Report->Attempts = FMath::Min(Report->Attempts + 1, 3);
	const int32 Attempt = Report->Attempts;
	const FDungeonMutationScope Scope = CaptureDungeonMutationScope();

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Report->Request = Request;
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetRecoveryOwner(),
		[this, Report, Attempt, Key, Scope](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response,
			bool bTransportSucceeded)
		{
			const auto* Current = DungeonDisconnectionReports.Find(Key);
			if (bDungeonDisconnectionReportShutdown || !Current || Current->Get() != &Report.Get()
				|| CompletedRequest != Report->Request || !DungeonDisconnectionJournal) return;
			Report->Request.Reset();
			if (!IsCurrentDungeonMutationScope(Scope) || !DungeonDisconnectionJournal->MatchesAssignment(Scope.BackendUrl, Scope.ServerId, Scope.SessionId)) return;
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString ResponseBody = Response.IsValid() && Response->GetContentLength() <= 4096
				? Response->GetContentAsString() : FString();
			const bool bAccepted = bTransportSucceeded
				&& FRPGDungeonDisconnectionJournal::IsAcceptedResponse(ResponseBody, Status);
			if (bTransportSucceeded && (bAccepted || FRPGDungeonDisconnectionJournal::IsTerminalRejection(ResponseBody, Status))
				&& DungeonDisconnectionJournal->Resolve(Key))
			{
				DungeonDisconnectionReports.Remove(Key);
				return;
			}
			ScheduleDungeonDisconnectionRetry(Report, Attempt, bTransportSucceeded ? Status : 0);
		});
	Request->SetURL(Host.GetRecoveryApiUrl() / FString::Printf(
		TEXT("dungeon-sessions/%s/members/%s/disconnection"),
		*Report->DungeonSessionId, *Report->CharacterId));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Host.ApplyRecoveryServiceAuthorization(Request);
	Request->SetContentAsString(Report->Body);
	Request->SetTimeout(FMath::IsFinite(Host.GetRecoveryRequestTimeout())
		? FMath::Clamp(Host.GetRecoveryRequestTimeout(), 1.f, 30.f) : 10.f);
	return Request;
}

void FRPGDungeonConnectionRecovery::OnConsumeJoinTicketResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString JoinTicket)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(JoinTicket);
	if (!Recovery) return;
	if (!IsCurrentDungeonMutationScope(Recovery->Scope))
	{
		DungeonConsumeRecoveries.Remove(JoinTicket);
		return;
	}
	if (FPlatformTime::Seconds() >= Recovery->Deadline)
	{
		ExpireDungeonConsumeRecovery(JoinTicket, Recovery.ToSharedRef());
		return;
	}
	const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());
	FRPGJoinTicketAdmission Admission;
	bool bDecoded = false;

	if (bSuccess)
	{
		bDecoded = Response->GetContentLength() <= 8192
			&& FRPGJoinTicketAdmission::Decode(Response->GetContentAsString(), Admission);
		bSuccess = bDecoded && Admission.DungeonSessionId.Equals(Recovery->Scope.SessionId, ESearchCase::CaseSensitive);
	}
	const bool bUncertain = !bWasSuccessful || !Response.IsValid() || Status <= 0
		|| (Status == 401 && Recovery->bUncertain && !IsDefinitiveDungeonConsumeDenial(Response))
		|| Status == 408 || Status == 425 || Status == 429 || Status >= 500
		|| (EHttpResponseCodes::IsOk(Status) && !bSuccess);
	if (!bSuccess && bUncertain)
	{
		Recovery->bUncertain = true;
		Recovery->bWaiting = true;
		const double Delay = Recovery->Retries == 0 ? 1.0 : Recovery->Retries == 1 ? 3.0 : 30.0;
		++Recovery->Retries;
		Recovery->NextAttemptAt = FMath::Min(FPlatformTime::Seconds() + Delay, Recovery->Deadline);
		return; // Keep GameMode's exact original connection and first-loss observation alive.
	}
	if (Recovery->LostAtUtc.GetTicks() > 0)
	{
		// This peer is already gone. Even a successful late consume reply is historical
		// evidence, not permission to discard the durable loss or welcome a new socket.
		ExpireDungeonConsumeRecovery(JoinTicket, Recovery.ToSharedRef());
		if (bSuccess && Admission.SteamId.Equals(Recovery->ExpectedSteamId, ESearchCase::CaseSensitive))
		{
			Recovery->EvidenceCharacterId = Admission.CharacterId;
			Recovery->EvidenceGeneration = Admission.ConnectionGeneration;
			QueueDungeonConsumeEvidence(JoinTicket, Recovery.ToSharedRef());
		}
		return;
	}
	// Hold the uncertainty barrier until synchronous GameMode handoff has had a
	// chance to durably queue an already observed socket loss.
	TGuardValue<bool> CompletionGuard(bDungeonConsumeRecoveryCompleting,
		bDungeonConsumeRecoveryCompleting || Recovery->bUncertain);
	DungeonConsumeRecoveries.Remove(JoinTicket);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Join ticket validation failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	Host.BroadcastConsumedTicket(
		JoinTicket,
		Admission.DungeonSessionId,
		Admission.CharacterId,
		Admission.SteamId,
		Admission.ConnectionGeneration,
		bSuccess);
}
