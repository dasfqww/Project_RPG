#if WITH_DEV_AUTOMATION_TESTS

#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonDisconnectionJournal.h"
#include "Online/RPGDungeonConsumeJournal.h"
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringOutputDevice.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Tests/RPGDungeonEjectionTestFixture.h"

// Build real production requests and inject responses, never ProcessRequest.
struct FRPGDungeonDisconnectionOrderingTestAccess
{
	static void Configure(UHttpWebManager& Web, const TSharedPtr<FRPGDungeonDisconnectionJournal>& Journal,
		const FString& Session, const FString& Server)
	{
		Web.ConfiguredDungeonSessionId = Session; Web.GameServerId = Server;
		Web.BackendServiceToken = TEXT("ordering-server-fixture"); Web.DungeonDisconnectionJournal = Journal;
		if (!Web.DungeonConsumeJournal)
			Web.InitializeDungeonConsumeRecoveries(FPaths::Combine(Journal->GetDirectory(), TEXT("ConsumeRecoveries")));
	}
	static FHttpRequestPtr Consume(UHttpWebManager& Web, const FString& Ticket = TEXT("unconsumed-ticket-fixture"),
		const FString& Steam = FString())
	{ return Web.CreateConsumeJoinTicketRequest(Ticket, Steam); }
	static FHttpRequestPtr Settle(UHttpWebManager& Web)
	{ return Web.CreateDungeonRewardSettlementRequest(TEXT("ordering_reward_v1"), {}, {}); }
	static FHttpRequestPtr Report(UHttpWebManager& Web, const FRPGDungeonDisconnectionRecord& Record)
	{
		auto* Found = Web.DungeonDisconnectionReports.Find(Record.Key());
		if (!Found)
		{
			auto Report = MakeShared<UHttpWebManager::FDungeonDisconnectionReport>();
			Report->DungeonSessionId = Web.ConfiguredDungeonSessionId;
			Report->CharacterId = Record.CharacterId; Report->SteamId = Record.SteamId;
			Report->ConnectionGeneration = Record.ConnectionGeneration;
			Report->ObservedAtUtc = Record.ObservedAtUtc; Report->Body = Record.Body;
			Web.DungeonDisconnectionReports.Add(Record.Key(), Report);
			Found = Web.DungeonDisconnectionReports.Find(Record.Key());
		}
		return Web.CreateDungeonDisconnectionAttempt(Found->ToSharedRef());
	}
	static FHttpRequestPtr Membership(UHttpWebManager& Web)
	{ return Web.CreateConfiguredDungeonMembershipRequest([](auto) {}); }
	static void CancelMembership(UHttpWebManager& Web) { Web.CancelDungeonMembershipRequest(); }
	static void Shutdown(UHttpWebManager& Web) { Web.bDungeonDisconnectionReportShutdown = true; }
	static void ReplaceJournal(UHttpWebManager& Web, const TSharedPtr<FRPGDungeonDisconnectionJournal>& Journal)
	{ Web.DungeonDisconnectionJournal = Journal; }
	static void CancelMutations(UHttpWebManager& Web) { Web.CancelDungeonConnectionMutations(); }
	static FHttpRequestPtr ActiveConsume(UHttpWebManager& Web, const FString& Ticket)
	{ return Web.ActiveDungeonConsumeRequests.FindRef(Ticket); }
	static FHttpRequestPtr ActiveSettlement(UHttpWebManager& Web) { return Web.ActiveDungeonSettlementRequest; }
	static auto Recovery(UHttpWebManager& Web, const FString& Ticket) { return Web.DungeonConsumeRecoveries.FindRef(Ticket); }
	static FHttpRequestPtr Retry(UHttpWebManager& Web, const FString& Ticket)
	{
		if (auto State = Recovery(Web, Ticket)) State->NextAttemptAt = 0.0;
		return Web.TakeDueDungeonConsumeRetry(Ticket, FPlatformTime::Seconds());
	}
	static void ExpireRecovery(UHttpWebManager& Web, const FString& Ticket)
	{
		if (auto State = Recovery(Web, Ticket)) State->Deadline = FPlatformTime::Seconds() - 1.0;
		Web.TakeDueDungeonConsumeRetry(Ticket, FPlatformTime::Seconds());
	}
	static FHttpRequestPtr Evidence(UHttpWebManager& Web, const FString& Ticket)
	{
		if (auto State = Recovery(Web, Ticket)) State->NextAttemptAt = 0.0;
		return Web.TakeDueDungeonConsumeEvidence(Ticket, FPlatformTime::Seconds());
	}
	static auto ConsumeJournal(UHttpWebManager& Web) { return Web.DungeonConsumeJournal; }
	static FHttpRequestPtr EvidenceAt(UHttpWebManager& Web, const FString& Ticket, double Now)
	{ return Web.TakeDueDungeonConsumeEvidence(Ticket, Now); }
	static void MarkConsumePersistenceFailed(UHttpWebManager& Web) { Web.bDungeonConsumePersistenceFailed = true; }
	static int32 PendingObservationWrites(UHttpWebManager& Web) { return Web.PendingDungeonConsumeObservationWrites.Num(); }
	static double ObservationWriteDue(UHttpWebManager& Web) { return Web.NextDungeonConsumeObservationWriteAt; }
	static void RetryObservationWrites(UHttpWebManager& Web, double Now) { Web.RetryDungeonConsumeObservationWrites(Now); }
	static int32 PendingDisconnectionWrites(UHttpWebManager& Web) { return Web.PendingDungeonDisconnectionWrites.Num(); }
	static double DisconnectionWriteDue(UHttpWebManager& Web) { return Web.NextDungeonDisconnectionWriteAt; }
	static void RetryDisconnectionWrites(UHttpWebManager& Web, double Now) { Web.RetryDungeonDisconnectionWrites(Now); }
	static auto StoredReport(UHttpWebManager& Web, const FString& Key) { return Web.DungeonDisconnectionReports.FindRef(Key); }
	static void ServiceToken(UHttpWebManager& Web, const FString& Token) { Web.BackendServiceToken = Token; }
	static void MarkDisconnectionPersistenceFailed(UHttpWebManager& Web) { Web.bDungeonDisconnectionPersistenceFailed = true; }
	static void BindRestoredWorld(UHttpWebManager& Web) { Web.BindRestoredDungeonConsumeWorld(); }
	static void InitializeJournals(UHttpWebManager& Web, const FString& Directory, const FString& Session, const FString& Server)
	{
		Web.ConfiguredDungeonSessionId = Session; Web.GameServerId = Server;
		Web.BackendServiceToken = TEXT("ordering-server-fixture");
		Web.InitializeDungeonDisconnectionReports(Directory);
	}
	static auto DisconnectionJournal(UHttpWebManager& Web) { return Web.DungeonDisconnectionJournal; }
	static bool ParentRestorePending(UHttpWebManager& Web) { return Web.bDungeonDisconnectionRestorePending; }
	static bool ConsumeRestorePending(UHttpWebManager& Web) { return Web.bDungeonConsumeRestorePending; }
	static double RestoreDue(UHttpWebManager& Web) { return Web.NextDungeonJournalRestoreAt; }
	static void RetryRestores(UHttpWebManager& Web, double Now) { Web.RetryDungeonJournalRestores(Now); }
	static bool TickerArmed(UHttpWebManager& Web) { return Web.DungeonDisconnectionTicker.IsValid(); }
	static void StopTicker(UHttpWebManager& Web)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Web.DungeonDisconnectionTicker);
		Web.DungeonDisconnectionTicker.Reset();
	}
	static int32 ReportCount(UHttpWebManager& Web) { return Web.DungeonDisconnectionReports.Num(); }
	static int32 RecoveryCount(UHttpWebManager& Web) { return Web.DungeonConsumeRecoveries.Num(); }
	static void TickRestore(UHttpWebManager& Web)
	{ Web.NextDungeonJournalRestoreAt = 0.0; Web.TickDungeonDisconnectionReports(0.f); }
	static void ConfigureHttp(UHttpWebManager& Web, const FString& Backend, const FString& Token)
	{ Web.ApiUrl = Backend; Web.BackendServiceToken = Token; }
	static void DispatchConsume(UHttpWebManager& Web, const FString& Ticket, const FHttpRequestPtr& Request)
	{ Web.DispatchDungeonConsumeAttempt(Ticket, Request); }
};

namespace RPGDisconnectionOrderingTests
{
	class FResponse final : public IHttpResponse
	{
	public:
		FResponse(int32 InCode, FString InBody) : Code(InCode), Body(MoveTemp(InBody))
		{
			const FTCHARToUTF8 Utf8(*Body);
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		const FString& GetURL() const override { return Url; }
		const FString& GetEffectiveURL() const override { return Url; }
		EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
		EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
		FString GetURLParameter(const FString&) const override { return {}; }
		FString GetHeader(const FString&) const override { return {}; }
		TArray<FString> GetAllHeaders() const override { return {}; }
		FString GetContentType() const override { return TEXT("application/json"); }
		uint64 GetContentLength() const override { return Bytes.Num(); }
		const TArray<uint8>& GetContent() const override { return Bytes; }
		TArray<uint8> TakeContent() override { return MoveTemp(Bytes); }
		int32 GetResponseCode() const override { return Code; }
		FString GetContentAsString() const override { return Body; }
		FUtf8StringView GetContentAsUtf8StringView() const override
		{ return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	private:
		int32 Code;
		FString Body, Url = TEXT("http://ordering-fixture.invalid/api");
		TArray<uint8> Bytes;
	};
	void Complete(const FHttpRequestPtr& Request, int32 Status, const FString& Body, bool bTransport = true)
	{
		FHttpResponsePtr Response = MakeShared<FResponse, ESPMode::ThreadSafe>(Status, Body);
		Request->OnProcessRequestComplete().ExecuteIfBound(Request, Response, bTransport);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDisconnectionOrderingTest,
	"ProjectRPG.Online.DungeonDisconnectionOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonDisconnectionOrderingTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	const FString Server = TEXT("ordering-server-fixture");
	const FString Backend = TEXT("http://localhost:3000/api");
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonDisconnectionOrdering"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Web = nullptr;
	ON_SCOPE_EXIT { if (Web) Access::CancelMutations(*Web); FCommandLine::Set(*PreviousCommandLine);
		IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	UGameInstance* Instance = NewObject<UGameInstance>();
	Web = NewObject<UHttpWebManager>(Instance);
	TestFalse(TEXT("An uninitialized outbox is never ready"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!TestTrue(TEXT("Fresh journal restored"), Journal->Restore())) return false;
	Access::Configure(*Web, Journal, Session, Server);
	TestTrue(TEXT("Healthy empty assigned outbox is ready"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	TestTrue(TEXT("Ready admission builds without consuming a real ticket"), Access::Consume(*Web).IsValid());
	FHttpRequestPtr InitialSettlement = Access::Settle(*Web);
	if (!TestTrue(TEXT("Ready settlement builds without dispatch"), InitialSettlement.IsValid())) return false;
	const TArray<uint8> OriginalRewardBody = InitialSettlement->GetContent();
	InitialSettlement.Reset();
	Access::CancelMutations(*Web); // These probes prepared requests but never dispatched them.

	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	Record.SteamId = TEXT("76561198000000001"); Record.ConnectionGeneration = 9007199254740993LL;
	Record.ObservedAtUtc = FDateTime(2026, 10, 19, 1, 2, 3, 456);
	Record.EncodeBody(Session, Record.Body);
	if (!TestTrue(TEXT("Observed loss is durably queued"), Journal->Persist(Record))) return false;
	auto CheckBlocked = [&]()
	{
		TestFalse(TEXT("Pending loss blocks mutations"), Web->CanDispatchConfiguredDungeonConnectionMutation());
		TestFalse(TEXT("Pending loss cannot build ticket consumption"), Access::Consume(*Web).IsValid());
		TestFalse(TEXT("Pending loss cannot build reward settlement"), Access::Settle(*Web).IsValid());
	};
	CheckBlocked(); // Includes disk-only pending entries before the replay map is populated.
	auto Restored = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	TestTrue(TEXT("Restart restores durable barrier"), Restored->Restore());
	Journal = Restored; Access::ReplaceJournal(*Web, Journal); CheckBlocked();
	FHttpRequestPtr Membership = Access::Membership(*Web);
	TestTrue(TEXT("Read-only membership recovery stays available"), Membership.IsValid());
	Access::CancelMembership(*Web);
	const FString Accepted = TEXT("{\"disconnectedAt\":\"2026-10-19T01:02:03.456Z\"}");
	for (int32 Status : { 0, 200, 400, 401, 429, 500 })
	{
		FHttpRequestPtr Request = Access::Report(*Web, Record);
		if (!TestTrue(TEXT("Retry prepares the real report callback"), Request.IsValid())) return false;
		CheckBlocked();
		Complete(Request, Status, Status == 0 ? Accepted : TEXT("{}"), Status != 0);
		CheckBlocked();
	}
	FHttpRequestPtr OldRequest = Access::Report(*Web, Record);
	if (!TestTrue(TEXT("Old attempt prepared"), OldRequest.IsValid())) return false;
	Complete(OldRequest, 500, TEXT("{}"));
	FHttpRequestPtr CurrentRequest = Access::Report(*Web, Record);
	if (!TestTrue(TEXT("New attempt prepared"), CurrentRequest.IsValid())) return false;
	Complete(OldRequest, 200, Accepted);
	TestFalse(TEXT("Stale completion cannot release a new attempt"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Complete(CurrentRequest, 200, Accepted);
	TestTrue(TEXT("Only accepted report and durable file removal release the barrier"),
		Web->CanDispatchConfiguredDungeonConnectionMutation());
	TestFalse(TEXT("Accepted file was removed"), IFileManager::Get().FileExists(*FPaths::Combine(Directory, Record.Key() + TEXT(".json"))));
	FHttpRequestPtr ResumedSettlement = Access::Settle(*Web);
	if (TestTrue(TEXT("Settlement resumes after resolution"), ResumedSettlement.IsValid()))
		TestTrue(TEXT("Deferral preserved exact reward request body"), ResumedSettlement->GetContent() == OriginalRewardBody);
	TestTrue(TEXT("Admission resumes after resolution"), Access::Consume(*Web).IsValid());
	Access::CancelMutations(*Web);

	FRPGDungeonDisconnectionRecord Other = Record;
	Other.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f003"); Other.EncodeBody(Session, Other.Body);
	TestTrue(TEXT("Two party losses queued"), Journal->Persist(Record) && Journal->Persist(Other));
	FHttpRequestPtr First = Access::Report(*Web, Record);
	if (!First.IsValid()) return false;
	Complete(First, 403, TEXT("")); CheckBlocked();
	FHttpRequestPtr Second = Access::Report(*Web, Other);
	if (!Second.IsValid()) return false;
	Complete(Second, 200, Accepted);
	TestTrue(TEXT("Every observed loss must resolve before settlement"), Web->CanDispatchConfiguredDungeonConnectionMutation());

	// Collision mimics a failed durable write without sending HTTP or deleting user data.
	const FString Collision = FPaths::Combine(Directory, Record.Key() + TEXT(".json.tmp"));
	if (!TestTrue(TEXT("Create interrupted fixture write"), FFileHelper::SaveStringToFile(TEXT("interrupted"), *Collision))) return false;
	TestFalse(TEXT("Failed persistence is not reported as queued"), Web->ReportConfiguredDungeonMemberDisconnection(
		Session, Record.CharacterId, Record.SteamId, Record.ConnectionGeneration, Record.ObservedAtUtc));
	CheckBlocked();
	IFileManager::Get().Delete(*Collision);
	TestTrue(TEXT("Journal can be inspected after removing only the corrupt fixture"), Journal->Restore());
	CheckBlocked(); // A tracked unflushed report is not evidence of delivery, even with an empty healthy directory.
	Access::Shutdown(*Web); CheckBlocked();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonInFlightMutationTest,
	"ProjectRPG.Online.DungeonInFlightMutationCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonInFlightMutationTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	const FString Server = TEXT("ordering-server-fixture");
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonInFlightMutation"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Web = nullptr;
	ON_SCOPE_EXIT { if (Web) Access::CancelMutations(*Web); FCommandLine::Set(*PreviousCommandLine);
		IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	UGameInstance* Instance = NewObject<UGameInstance>();
	Web = NewObject<UHttpWebManager>(Instance);
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, TEXT("http://localhost:3000/api"), Server, Session);
	if (!TestTrue(TEXT("Empty journal ready"), Journal->Restore())) return false;
	Access::Configure(*Web, Journal, Session, Server);
	auto* Observer = NewObject<URPGDungeonMutationTestObserver>();
	Web->OnJoinTicketConsumed.AddDynamic(Observer, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	Web->OnDungeonRewardSettlementRequested.AddDynamic(Observer, &URPGDungeonMutationTestObserver::SettlementCompleted);
	const FString Ticket = TEXT("in-flight-ticket-fixture");
	FHttpRequestPtr Consume = Access::Consume(*Web, Ticket), Settle = Access::Settle(*Web);
	if (!TestTrue(TEXT("Prepare independent in-flight operations without dispatch"), Consume.IsValid() && Settle.IsValid())) return false;
	TestFalse(TEXT("Same ticket cannot start a second consume"), Access::Consume(*Web, Ticket).IsValid());
	TestFalse(TEXT("Settlement cannot start a second operation"), Access::Settle(*Web).IsValid());
	Web->ConsumeJoinTicket(Ticket);
	Web->SettleConfiguredDungeonRewards(TEXT("ordering_reward_v1"), {}, {});
	TestEqual(TEXT("Duplicate callers do not fail the original admission"), Observer->ConsumeReplies, 0);
	TestEqual(TEXT("Duplicate callers do not reset original settlement state"), Observer->SettlementReplies, 0);

	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	Record.SteamId = TEXT("76561198000000001"); Record.ConnectionGeneration = 42;
	Record.ObservedAtUtc = FDateTime(2026, 10, 19, 1, 2, 3, 456); Record.EncodeBody(Session, Record.Body);
	if (!TestTrue(TEXT("Loss observed after request dispatch is retained"), Journal->Persist(Record))) return false;
	Web->SettleConfiguredDungeonRewards(TEXT("ordering_reward_v1"), {}, {});
	TestEqual(TEXT("A new loss does not inject a false 425 into an active operation"), Observer->SettlementReplies, 0);
	TestTrue(TEXT("An observed loss never cancels an already sent request"),
		Access::ActiveConsume(*Web, Ticket) == Consume && Access::ActiveSettlement(*Web) == Settle);
	const FString Admission = FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":43}"),
		*Session, *Record.CharacterId, *Record.SteamId);
	const FString Settlement = FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"rewardVersion\":\"ordering_reward_v1\",\"state\":\"Pending\"}"), *Session);
	Complete(Consume, 200, Admission);
	Complete(Settle, 202, Settlement);
	TestTrue(TEXT("Committed admission is delivered despite a later pending loss"), Observer->bLastConsumeSucceeded && Observer->ConsumeReplies == 1);
	TestTrue(TEXT("Committed settlement is delivered despite a later pending loss"), Observer->bLastSettlementSucceeded && Observer->SettlementReplies == 1);
	TestEqual(TEXT("Settlement acknowledgement is preserved"), Observer->LastSettlementState, FString(TEXT("Pending")));
	TestFalse(TEXT("Acknowledgement does not erase a later loss"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Complete(Consume, 500, TEXT("{}")); Complete(Settle, 500, TEXT("{}"));
	TestEqual(TEXT("Consume completion is applied once"), Observer->ConsumeReplies, 1);
	TestEqual(TEXT("Settlement completion is applied once"), Observer->SettlementReplies, 1);
	FHttpRequestPtr Report = Access::Report(*Web, Record);
	if (!Report.IsValid()) return false;
	Complete(Report, 200, TEXT("{\"disconnectedAt\":\"2026-10-19T01:02:03.456Z\"}"));
	FHttpRequestPtr NextConsume = Access::Consume(*Web, Ticket), NextSettle = Access::Settle(*Web);
	if (!TestTrue(TEXT("Later operation can start after reports resolve"), NextConsume.IsValid() && NextSettle.IsValid())) return false;
	Complete(Consume, 500, TEXT("{}")); Complete(Settle, 500, TEXT("{}"));
	TestTrue(TEXT("Old completions cannot clear a later in-flight operation"),
		Access::ActiveConsume(*Web, Ticket) == NextConsume && Access::ActiveSettlement(*Web) == NextSettle);
	TestTrue(TEXT("Old completions cannot broadcast into a later operation"), Observer->ConsumeReplies == 1 && Observer->SettlementReplies == 1);
	Complete(NextConsume, 200, Admission.Replace(*Session, TEXT("10203040-5060-7080-90a0-b0c0d0e0f099")));
	Complete(NextSettle, 500, TEXT("{}"));
	TestEqual(TEXT("Wrong-session admission cannot finish the original admission"), Observer->ConsumeReplies, 1);
	FHttpRequestPtr DeniedRetry = Access::Retry(*Web, Ticket);
	if (!TestTrue(TEXT("Unusable response remains recoverable with the same intent"), DeniedRetry.IsValid())) return false;
	Complete(DeniedRetry, 400, TEXT("{}"));
	TestTrue(TEXT("Current definitive denial is delivered once"), Observer->ConsumeReplies == 2 && !Observer->bLastConsumeSucceeded);
	TestTrue(TEXT("Current failure is delivered for retry"), Observer->SettlementReplies == 2 && !Observer->bLastSettlementSucceeded);

	FHttpRequestPtr ScopedConsume = Access::Consume(*Web, Ticket), ScopedSettle = Access::Settle(*Web);
	if (!ScopedConsume.IsValid() || !ScopedSettle.IsValid()) return false;
	Access::Configure(*Web, Journal, Session, Server.ToUpper());
	Complete(ScopedConsume, 200, Admission); Complete(ScopedSettle, 202, Settlement);
	TestTrue(TEXT("Old assignment responses never broadcast into a new assignment"), Observer->ConsumeReplies == 2 && Observer->SettlementReplies == 2);
	Access::Configure(*Web, Journal, Session, Server);
	FHttpRequestPtr ClosingConsume = Access::Consume(*Web, Ticket), ClosingSettle = Access::Settle(*Web);
	if (!ClosingConsume.IsValid() || !ClosingSettle.IsValid()) return false;
	const auto QueuedConsumeCallback = ClosingConsume->OnProcessRequestComplete();
	const auto QueuedSettleCallback = ClosingSettle->OnProcessRequestComplete();
	TestTrue(TEXT("A pending loss exists at shutdown"), Journal->Persist(Record));
	Web->Deinitialize();
	TestFalse(TEXT("Shutdown unbinds consume transport"), ClosingConsume->OnProcessRequestComplete().IsBound());
	TestFalse(TEXT("Shutdown unbinds settlement transport"), ClosingSettle->OnProcessRequestComplete().IsBound());
	FHttpResponsePtr AcceptedAdmission = MakeShared<FResponse, ESPMode::ThreadSafe>(200, Admission);
	FHttpResponsePtr AcceptedSettlement = MakeShared<FResponse, ESPMode::ThreadSafe>(202, Settlement);
	QueuedConsumeCallback.ExecuteIfBound(ClosingConsume, AcceptedAdmission, true);
	QueuedSettleCallback.ExecuteIfBound(ClosingSettle, AcceptedSettlement, true);
	TestTrue(TEXT("Already queued callbacks cannot escape shutdown"), Observer->ConsumeReplies == 2 && Observer->SettlementReplies == 2);
	TestEqual(TEXT("Shutdown does not erase observed loss evidence"), Journal->GetPending().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConsumeRecoveryTest,
	"ProjectRPG.Online.DungeonConsumeResponseRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConsumeRecoveryTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("consume-recovery-fixture");
	const FString Ticket = TEXT("original-consume-recovery-ticket");
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonConsumeRecovery"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Web = nullptr;
	ON_SCOPE_EXIT { if (Web) Access::CancelMutations(*Web); FCommandLine::Set(*PreviousCommandLine);
		IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	UGameInstance* Instance = NewObject<UGameInstance>(); Web = NewObject<UHttpWebManager>(Instance);
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, TEXT("http://localhost:3000/api"), Server, Session);
	if (!TestTrue(TEXT("Empty recovery outbox ready"), Journal->Restore())) return false;
	Access::Configure(*Web, Journal, Session, Server);
	auto* Observer = NewObject<URPGDungeonMutationTestObserver>(); Observer->InspectConsumeGuard = Web;
	Web->OnJoinTicketConsumed.AddDynamic(Observer, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	FHttpRequestPtr First = Access::Consume(*Web, Ticket);
	if (!TestTrue(TEXT("Production initial request built without HTTP dispatch"), First.IsValid())) return false;
	auto State = Access::Recovery(*Web, Ticket);
	if (!TestTrue(TEXT("Original intent state retained"), State.IsValid())) return false;
	const FString OriginalId = State->RequestId;
	const double OriginalDeadline = State->Deadline;
	FGuid ParsedId;
	TestTrue(TEXT("One valid UUID is included in the request body"),
		FGuid::ParseExact(OriginalId, EGuidFormats::DigitsWithHyphensLower, ParsedId) && ParsedId.IsValid()
		&& State->Body.Contains(TEXT("\"consumptionRequestId\":\"") + OriginalId + TEXT("\"")));
	const TArray<uint8> OriginalBody = First->GetContent();
	Complete(First, 0, TEXT(""), false);
	TestTrue(TEXT("Lost response retains intent, schedules retry and does not fail PreLogin"),
		Access::Recovery(*Web, Ticket) == State && State->bWaiting && State->bUncertain && Observer->ConsumeReplies == 0);
	TestEqual(TEXT("First transient schedules the one-second retry"), State->Retries, 1);
	TestFalse(TEXT("Unknown consumption blocks new admission"), Access::Consume(*Web, TEXT("other-ticket")).IsValid());
	TestFalse(TEXT("Unknown consumption blocks new settlement"), Access::Settle(*Web).IsValid());
	Web->ConsumeJoinTicket(Ticket);
	TestEqual(TEXT("Duplicate public caller cannot fail or replace a waiting admission"), Observer->ConsumeReplies, 0);
	TestTrue(TEXT("Duplicate preserves UUID and first-send deadline"), State->RequestId == OriginalId && State->Deadline == OriginalDeadline);
	TestTrue(TEXT("Read-only membership recovery is still allowed"), Access::Membership(*Web).IsValid());
	Access::CancelMembership(*Web);

	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	Record.SteamId = TEXT("76561198000000001"); Record.ConnectionGeneration = 42;
	Record.ObservedAtUtc = FDateTime(2026, 10, 19, 1, 2, 3, 456); Record.EncodeBody(Session, Record.Body);
	if (!TestTrue(TEXT("Another observed loss is durably queued"), Journal->Persist(Record))) return false;
	TestFalse(TEXT("Retry cannot bypass an existing disconnect outbox"), Access::Retry(*Web, Ticket).IsValid());
	TestTrue(TEXT("Waiting on outbox never replaces the original intent"), Access::Recovery(*Web, Ticket) == State);
	FHttpRequestPtr Report = Access::Report(*Web, Record);
	if (!Report.IsValid()) return false;
	Complete(Report, 200, TEXT("{\"disconnectedAt\":\"2026-10-19T01:02:03.456Z\"}"));
	FHttpRequestPtr Retry = Access::Retry(*Web, Ticket);
	if (!TestTrue(TEXT("Same-intent retry resumes after report acceptance"), Retry.IsValid())) return false;
	TestTrue(TEXT("New HTTP object reuses the exact body, UUID and deadline"),
		Retry != First && Retry->GetContent() == OriginalBody && State->RequestId == OriginalId && State->Deadline == OriginalDeadline);
	const FString Admission = FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":9007199254740993}"),
		*Session, *Record.CharacterId, *Record.SteamId);
	Complete(First, 200, Admission);
	TestTrue(TEXT("Late original callback cannot claim retry completion"), Observer->ConsumeReplies == 0 && Access::ActiveConsume(*Web, Ticket) == Retry);
	for (int32 Status : {401, 429, 503, 200})
	{
		Complete(Retry, Status, TEXT("{}"));
		TestEqual(TEXT("Ambiguous retry response does not discard original PreLogin state"), Observer->ConsumeReplies, 0);
		TestTrue(TEXT("Each retry keeps the same original intent"), Access::Recovery(*Web, Ticket) == State && State->Deadline == OriginalDeadline);
		Retry = Access::Retry(*Web, Ticket);
		if (!TestTrue(TEXT("Next retry can be built without transmitting"), Retry.IsValid())) return false;
		TestTrue(TEXT("Every retry has the identical request payload"), Retry->GetContent() == OriginalBody);
	}
	TestEqual(TEXT("Backoff caps at 30 seconds without multiplying intents"), State->Retries, 5);
	Complete(Retry, 200, Admission);
	TestTrue(TEXT("Recovered exact-generation success is delivered once"), Observer->ConsumeReplies == 1 && Observer->bLastConsumeSucceeded);
	TestEqual(TEXT("Recovered generation is never rounded through a double"), Observer->LastConsumeGeneration, int64(9007199254740993LL));
	TestFalse(TEXT("Uncertainty barrier remains closed during synchronous handoff"), Observer->bMutationAllowedDuringConsumeReply);
	TestTrue(TEXT("Resolved intent is released only after handoff"), !Access::Recovery(*Web, Ticket) && Web->CanDispatchConfiguredDungeonConnectionMutation());
	Complete(Retry, 500, TEXT("{}"));
	TestEqual(TEXT("Duplicate completion does not schedule another recovery"), Observer->ConsumeReplies, 1);

	FHttpRequestPtr Denied = Access::Consume(*Web, Ticket);
	if (!Denied.IsValid()) return false;
	TestTrue(TEXT("A later independent intent gets its own UUID"), Access::Recovery(*Web, Ticket)->RequestId != OriginalId);
	Complete(Denied, 401, TEXT("{}"));
	TestTrue(TEXT("Initial definitive 401 fails promptly without wedging the session"),
		Observer->ConsumeReplies == 2 && !Observer->bLastConsumeSucceeded && Web->CanDispatchConfiguredDungeonConnectionMutation());
	FHttpRequestPtr Obsolete = Access::Consume(*Web, TEXT("obsolete-recovery-ticket"));
	if (!Obsolete.IsValid()) return false;
	Complete(Obsolete, 503, TEXT("{}"));
	Obsolete = Access::Retry(*Web, TEXT("obsolete-recovery-ticket"));
	if (!Obsolete.IsValid()) return false;
	Complete(Obsolete, 401, TEXT("{\"error\":\"join_ticket_not_recoverable\"}"));
	TestTrue(TEXT("Authenticated stale/revoked result resolves uncertainty without a ten-minute party lock"),
		Observer->ConsumeReplies == 3 && !Observer->bLastConsumeSucceeded && Web->CanDispatchConfiguredDungeonConnectionMutation());
	FHttpRequestPtr Scoped = Access::Consume(*Web, Ticket);
	if (!Scoped.IsValid()) return false;
	Complete(Scoped, 503, TEXT("{}"));
	Access::Configure(*Web, Journal, Session, Server.ToUpper());
	TestFalse(TEXT("A waiting recovery cannot migrate to a different assignment"), Access::Retry(*Web, Ticket).IsValid());
	TestEqual(TEXT("Old assignment does not broadcast into the new assignment"), Observer->ConsumeReplies, 3);
	Access::Configure(*Web, Journal, Session, Server);
	FHttpRequestPtr Expiring = Access::Consume(*Web, Ticket);
	FHttpRequestPtr Closing = Access::Consume(*Web, TEXT("closing-recovery-ticket"));
	if (!Expiring.IsValid() || !Closing.IsValid()) return false;
	const auto ClosingCallback = Closing->OnProcessRequestComplete();
	Complete(Expiring, 503, TEXT("{}"));
	Access::ExpireRecovery(*Web, Ticket);
	TestTrue(TEXT("Bounded unresolved recovery closes admission exactly once"), Observer->ConsumeReplies == 4 && !Observer->bLastConsumeSucceeded);
	Access::ExpireRecovery(*Web, Ticket);
	TestEqual(TEXT("Expiration cannot finish twice"), Observer->ConsumeReplies, 4);
	TestFalse(TEXT("Empty outbox after unresolved expiration is not permission to settle"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	TestTrue(TEXT("Expiration preserves the original intent for historical reconciliation"),
		Access::Recovery(*Web, Ticket).IsValid() && Web->HasExpiredDungeonConsumeRecovery(Ticket));
	TestTrue(TEXT("Operator-safe failed state still permits membership reads"), Access::Membership(*Web).IsValid());
	Access::CancelMembership(*Web);
	Web->Deinitialize();
	FHttpResponsePtr Accepted = MakeShared<FResponse, ESPMode::ThreadSafe>(200, Admission);
	ClosingCallback.ExecuteIfBound(Closing, Accepted, true);
	TestTrue(TEXT("Shutdown cancels transport and drops retry intents without a late success"),
		!Closing->OnProcessRequestComplete().IsBound() && !Access::Recovery(*Web, TEXT("closing-recovery-ticket")) && Observer->ConsumeReplies == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConsumeEvidenceTest,
	"ProjectRPG.Online.DungeonConsumeEvidenceRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConsumeEvidenceTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("consume-evidence-fixture");
	const FString Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"), Steam = TEXT("76561198000000001");
	const FString Ticket = TEXT("abc"), OtherTicket = TEXT("other-evidence-ticket");
	const FDateTime Loss(2026, 10, 19, 1, 2, 3, 456);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonConsumeEvidence"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Web = nullptr;
	ON_SCOPE_EXIT { if (Web) Access::CancelMutations(*Web); FCommandLine::Set(*PreviousCommandLine);
		IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	UGameInstance* Instance = NewObject<UGameInstance>(); Web = NewObject<UHttpWebManager>(Instance);
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, TEXT("http://localhost:3000/api"), Server, Session);
	if (!TestTrue(TEXT("Evidence test outbox restored"), Journal->Restore())) return false;
	Access::Configure(*Web, Journal, Session, Server);
	auto* Observer = NewObject<URPGDungeonMutationTestObserver>();
	Web->OnJoinTicketConsumed.AddDynamic(Observer, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	FHttpRequestPtr First = Access::Consume(*Web, Ticket, Steam), Other = Access::Consume(*Web, OtherTicket, Steam);
	if (!First.IsValid() || !Other.IsValid()) return false;
	auto State = Access::Recovery(*Web, Ticket), OtherState = Access::Recovery(*Web, OtherTicket);
	const FString RequestId = State->RequestId;
	TestEqual(TEXT("Backend-compatible UTF8 SHA256 uppercase hex, not SHA1 or double hash"), State->TicketHash,
		FString(TEXT("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD")));
	Web->ObserveDungeonConsumeConnectionLoss(Ticket, TEXT("76561198000000002"), Loss);
	TestEqual(TEXT("Another peer cannot attach a loss"), State->LostAtUtc.GetTicks(), int64(0));
	Web->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, Loss);
	Web->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, Loss + FTimespan::FromMinutes(20));
	Complete(First, 503, TEXT("{}")); Complete(Other, 0, TEXT(""), false);
	Access::ExpireRecovery(*Web, Ticket); Access::ExpireRecovery(*Web, OtherTicket);
	Access::ExpireRecovery(*Web, Ticket);
	TestEqual(TEXT("Two intents each deliver one failure, never a historical success"), Observer->ConsumeReplies, 2);
	TestTrue(TEXT("Expired intent preserves original UUID/peer/time and object"),
		Access::Recovery(*Web, Ticket) == State && State->RequestId == RequestId
		&& State->ExpectedSteamId == Steam && State->LostAtUtc == Loss);
	TestFalse(TEXT("No observed loss means no historical query or fabricated report"), Access::Evidence(*Web, OtherTicket).IsValid());
	auto EvidenceBody = [&](const FString& Id)
	{
		return FString::Printf(TEXT("{\"state\":\"Recorded\",\"consumption\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":9007199254740993,\"consumedAt\":\"2026-10-19T01:01:00+00:00\"}}"),
			*Session, *Server, *Id, *Character, *Steam);
	};
	const FString Valid = EvidenceBody(RequestId);
	FHttpRequestPtr Query = Access::Evidence(*Web, Ticket);
	if (!TestTrue(TEXT("Expired lost original can build its production evidence request"), Query.IsValid())) return false;
	const auto StaleCallback = Query->OnProcessRequestComplete();
	const FHttpRequestPtr StaleQuery = Query;
	const TArray<uint8> FrozenBody = Query->GetContent();
	const FUTF8ToTCHAR BodyText(reinterpret_cast<const ANSICHAR*>(FrozenBody.GetData()), FrozenBody.Num());
	const FString Body(BodyText.Length(), BodyText.Get());
	TestTrue(TEXT("Read body includes original peer/hash/UUID and no raw ticket or credentials"),
		Body.Contains(State->TicketHash) && Body.Contains(Steam) && Body.Contains(RequestId)
		&& !Body.Contains(TEXT("\"joinTicket\"")) && !Body.Contains(TEXT("ordering-server-fixture"))
		&& Query->GetURL().EndsWith(TEXT("/join-tickets/consumption-evidence")));
	for (int32 Status : {409, 401, 403, 429, 503})
	{
		Complete(Query, Status, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
		TestTrue(TEXT("Missing evidence/denial/offline never proves noncommit or unlocks mutations"),
			Access::Recovery(*Web, Ticket) == State && Journal->GetPending().IsEmpty()
			&& !Web->CanDispatchConfiguredDungeonConnectionMutation());
		Query = Access::Evidence(*Web, Ticket);
		if (!Query.IsValid()) return false;
		TestTrue(TEXT("Every evidence retry freezes original lookup body"), Query->GetContent() == FrozenBody);
	}
	FHttpResponsePtr Accepted = MakeShared<FResponse, ESPMode::ThreadSafe>(200, Valid);
	StaleCallback.ExecuteIfBound(StaleQuery, Accepted, true);
	TestTrue(TEXT("Old HTTP completion cannot claim a replacement lookup"), State->EvidenceRequest == Query);
	for (const FString& Bad : TArray<FString>{TEXT("{}"), Valid.Replace(TEXT("Recorded"), TEXT("recorded")),
		Valid.Replace(*RequestId, *OtherState->RequestId),
		Valid.Replace(*Steam, TEXT("76561198000000002")), Valid.Replace(*Server, TEXT("another-server")),
		Valid.Replace(*Session, TEXT("10203040-5060-7080-90a0-b0c0d0e0f099")),
		Valid.Replace(TEXT("9007199254740993"), TEXT("1.5")),
		Valid.Replace(TEXT("9007199254740993"), TEXT("\"7\"")),
		Valid.Replace(TEXT("+00:00"), TEXT("+09:00"))})
	{
		Complete(Query, 200, Bad);
		TestEqual(TEXT("Invalid or cross-intent evidence cannot supply a generation"), State->EvidenceGeneration, int64(0));
		Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	}
	Access::Configure(*Web, Journal, Session, Server.ToUpper());
	Complete(Query, 200, Valid);
	TestTrue(TEXT("Old-assignment evidence cannot enqueue or claim a generation"),
		Journal->GetPending().IsEmpty() && State->EvidenceGeneration == 0);
	Access::Configure(*Web, Journal, Session, Server);
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	Access::ReplaceJournal(*Web, nullptr);
	Complete(Query, 200, Valid);
	TestTrue(TEXT("Lookup success without durable handoff preserves the uncertainty"),
		Access::Recovery(*Web, Ticket) == State && State->EvidenceGeneration == 9007199254740993LL);
	TestEqual(TEXT("Evidence never broadcasts an admission success"), Observer->ConsumeReplies, 2);
	Access::ReplaceJournal(*Web, Journal);
	TestFalse(TEXT("Known evidence retries enqueue, not another lookup"), Access::Evidence(*Web, Ticket).IsValid());
	TestTrue(TEXT("Only the handed-off intent is released"), !Access::Recovery(*Web, Ticket) && Access::Recovery(*Web, OtherTicket) == OtherState);
	if (!TestEqual(TEXT("One exact durable report queued"), Journal->GetPending().Num(), 1)) return false;
	FRPGDungeonDisconnectionRecord Record;
	for (const auto& Entry : Journal->GetPending()) Record = Entry.Value;
	TestTrue(TEXT("Durable report uses original loss UTC/peer and exact historical int64, not consumedAt"),
		Record.ConnectionGeneration == 9007199254740993LL && Record.SteamId == Steam && Record.ObservedAtUtc == Loss);
	Query = Access::Report(*Web, Record); if (!Query.IsValid()) return false;
	Complete(Query, 200, TEXT("{\"disconnectedAt\":\"2026-10-19T01:02:03.456Z\"}"));
	TestFalse(TEXT("One resolved intent cannot unlock another unknown commit"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Web->ObserveDungeonConsumeConnectionLoss(OtherTicket, Steam, Loss);
	Query = Access::Evidence(*Web, OtherTicket); if (!Query.IsValid()) return false;
	Complete(Query, 200, EvidenceBody(OtherState->RequestId));
	TestTrue(TEXT("Last uncertainty transfers to outbox, not directly to admission"),
		!Access::Recovery(*Web, OtherTicket) && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	Query = Access::Report(*Web, Record); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{}"));
	TestFalse(TEXT("An unrecognized conflict cannot silently clear the outbox barrier"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Query = Access::Report(*Web, Record); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"dungeon_disconnection_not_available\"}"));
	TestTrue(TEXT("Mutations resume only after all intents handed off and outbox resolved"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	First = Access::Consume(*Web, TEXT("missing-original-peer"));
	FHttpRequestPtr Closing = Access::Consume(*Web, TEXT("closing-evidence"), Steam);
	if (!First.IsValid() || !Closing.IsValid()) return false;
	Complete(First, 503, TEXT("{}")); Access::ExpireRecovery(*Web, TEXT("missing-original-peer"));
	Web->ObserveDungeonConsumeConnectionLoss(TEXT("missing-original-peer"), Steam, Loss);
	TestFalse(TEXT("A response cannot backfill missing original Steam identity"), Access::Evidence(*Web, TEXT("missing-original-peer")).IsValid());
	TestFalse(TEXT("Missing original identity remains fail closed"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Complete(Closing, 503, TEXT("{}"));
	Web->ObserveDungeonConsumeConnectionLoss(TEXT("closing-evidence"), Steam, Loss);
	Access::ExpireRecovery(*Web, TEXT("closing-evidence"));
	Query = Access::Evidence(*Web, TEXT("closing-evidence")); if (!Query.IsValid()) return false;
	const auto ClosingCallback = Query->OnProcessRequestComplete();
	const FString ClosingId = Access::Recovery(*Web, TEXT("closing-evidence"))->RequestId;
	Web->Deinitialize();
	FHttpResponsePtr ClosingReply = MakeShared<FResponse, ESPMode::ThreadSafe>(200, EvidenceBody(ClosingId));
	ClosingCallback.ExecuteIfBound(Query, ClosingReply, true);
	TestTrue(TEXT("Shutdown cancels evidence transport; late success cannot fabricate a disconnection"),
		!Query->OnProcessRequestComplete().IsBound() && Journal->GetPending().IsEmpty()
		&& !Access::Recovery(*Web, TEXT("closing-evidence")) && Observer->ConsumeReplies == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConsumeRestartTest,
	"ProjectRPG.Online.DungeonConsumeObservedLossRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConsumeRestartTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("consume-restart-fixture");
	const FString Steam = TEXT("76561198000000001"), Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	const FString Backend = TEXT("http://localhost:3000/api"), Ticket = TEXT("restart-observed-original-ticket-fixture");
	const FDateTime Loss(2026, 10, 20, 1, 2, 3, 456);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonConsumeRestart"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Original = nullptr; UHttpWebManager* Restarted = nullptr;
	URPGDungeonMutationTestGameInstance* RestartedInstance = nullptr;
	UWorld* World = nullptr;
	ON_SCOPE_EXIT {
		if (Original) Original->Deinitialize(); if (Restarted) Restarted->Deinitialize();
		if (RestartedInstance) RestartedInstance->SetTestWorldContext(nullptr);
		if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		FCommandLine::Set(*PreviousCommandLine); IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	auto Outbox = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!Outbox->Restore()) return false;
	Original = NewObject<UHttpWebManager>(NewObject<UGameInstance>());
	Access::Configure(*Original, Outbox, Session, Server);
	FHttpRequestPtr Consume = Access::Consume(*Original, Ticket, Steam);
	FHttpRequestPtr Live = Access::Consume(*Original, TEXT("unobserved-live-ticket-fixture"), Steam);
	if (!Consume.IsValid() || !Live.IsValid()) return false;
	const auto Before = Access::Recovery(*Original, Ticket);
	const FString RequestId = Before->RequestId, Hash = Before->TicketHash;
	const FString RestoredKey = TEXT("restored:") + RequestId;
	Original->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, Loss);
	Complete(Consume, 0, TEXT(""), false);
	TestEqual(TEXT("Only real observed loss is saved, not the live pending peer"), Access::ConsumeJournal(*Original)->GetPending().Num(), 1);
	FRPGDungeonDisconnectionRecord Report;
	Report.CharacterId = Character; Report.SteamId = Steam; Report.ConnectionGeneration = 9007199254740993LL;
	Report.ObservedAtUtc = Loss; Report.EncodeBody(Session, Report.Body);
	// Crash window: a previous evidence lookup flushed the outbox, but did not yet
	// remove the pending-consumption file. Both durable copies must recover safely.
	if (!Outbox->Persist(Report)) return false;
	const FString LossPath = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), RequestId + TEXT(".json"));
	FString BeforeRestart;
	if (!FFileHelper::LoadFileToString(BeforeRestart, *LossPath)) return false;
	TestTrue(TEXT("No raw ticket or service credential is saved"),
		!BeforeRestart.Contains(Ticket) && !BeforeRestart.Contains(TEXT("ordering-server-fixture")) && BeforeRestart.Contains(Hash));
	Original->Deinitialize(); Original = nullptr;
	auto RestoredOutbox = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!RestoredOutbox->Restore()) return false;
	RestartedInstance = NewObject<URPGDungeonMutationTestGameInstance>();
	Restarted = NewObject<UHttpWebManager>(RestartedInstance);
	Access::Configure(*Restarted, RestoredOutbox, Session, Server);
	auto* Observer = NewObject<URPGDungeonMutationTestObserver>();
	Restarted->OnJoinTicketConsumed.AddDynamic(Observer, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	const auto Recovered = Access::Recovery(*Restarted, RestoredKey);
	if (!TestTrue(TEXT("New manager restores original intent without any admission callback"), Recovered.IsValid())) return false;
	TestTrue(TEXT("Restart retains exact original nonce/hash/peer/loss, not restart time"),
		Recovered->RequestId == RequestId && Recovered->TicketHash == Hash
		&& Recovered->ExpectedSteamId == Steam && Recovered->LostAtUtc == Loss && Recovered->Body.IsEmpty());
	TestFalse(TEXT("No replay of the original consumable ticket after restart"), Access::Retry(*Restarted, RestoredKey).IsValid());
	TestFalse(TEXT("Subsystem restoration before a game world cannot query evidence"), Access::Evidence(*Restarted, RestoredKey).IsValid());
	World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!World) return false;
	auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World); Context.OwningGameInstance = RestartedInstance;
	World->SetGameInstance(RestartedInstance); RestartedInstance->SetTestWorldContext(&Context);
	Access::BindRestoredWorld(*Restarted);
	TestTrue(TEXT("A Game-typed initialization/dummy world is not a gameplay recovery scope"), Recovered->bRestoredWorldPending);
	World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
	Access::BindRestoredWorld(*Restarted);
	TestTrue(TEXT("Restored intent adopts the first real game world"), !Recovered->bRestoredWorldPending && Recovered->Scope.World.Get() == World);
	World->bIsTearingDown = true;
	TestFalse(TEXT("Teardown cannot dispatch recovered evidence"), Access::Evidence(*Restarted, RestoredKey).IsValid());
	World->bIsTearingDown = false;
	FHttpRequestPtr Query = Access::Evidence(*Restarted, RestoredKey);
	if (!TestTrue(TEXT("Evidence query resumes without any HTTP dispatch in the fixture"), Query.IsValid())) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	FString AfterMissing;
	TestTrue(TEXT("Missing evidence preserves the original durable file and mutation barrier"),
		FFileHelper::LoadFileToString(AfterMissing, *LossPath) && AfterMissing == BeforeRestart
		&& !Restarted->CanDispatchConfiguredDungeonConnectionMutation());
	Query = Access::Evidence(*Restarted, RestoredKey); if (!Query.IsValid()) return false;
	TestTrue(TEXT("Restored original intent seals the same hash/UUID after an exact unresolved lookup"),
		Query->GetURL().EndsWith(TEXT("/join-tickets/seal-consumption")) && Recovered->Body.IsEmpty());
	const FString Evidence = FString::Printf(TEXT("{\"state\":\"Recorded\",\"consumption\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":9007199254740993,\"consumedAt\":\"2026-10-20T01:01:00Z\"}}"),
		*Session, *Server, *RequestId, *Character, *Steam);
	Complete(Query, 200, Evidence);
	TestTrue(TEXT("Verified evidence transfers durable ownership before removing the recovered intent"),
		!Access::Recovery(*Restarted, RestoredKey) && Access::ConsumeJournal(*Restarted)->GetPending().IsEmpty()
		&& !IFileManager::Get().FileExists(*LossPath) && RestoredOutbox->GetPending().Num() == 1);
	TestTrue(TEXT("Existing outbox body/time/generation is preserved across the crash window"),
		RestoredOutbox->GetPending().FindChecked(Report.Key()).Body == Report.Body);
	TestEqual(TEXT("Historical recovery never admits or activates a character"), Observer->ConsumeReplies, 0);
	Query = Access::Report(*Restarted, Report); if (!Query.IsValid()) return false;
	Complete(Query, 200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	TestTrue(TEXT("Admission/settlement reopen only after both durable journals resolve"), Restarted->CanDispatchConfiguredDungeonConnectionMutation());
	Consume = Access::Consume(*Restarted, Ticket, Steam); if (!Consume.IsValid()) return false;
	Restarted->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, Loss);
	const FString Admission = FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":9007199254740993}"),
		*Session, *Character, *Steam);
	Complete(Consume, 200, Admission);
	TestTrue(TEXT("Late success for an already lost peer hands off the saved loss instead of admitting"),
		Observer->ConsumeReplies == 1 && !Observer->bLastConsumeSucceeded
		&& Access::ConsumeJournal(*Restarted)->GetPending().IsEmpty() && RestoredOutbox->GetPending().Num() == 1);
	Query = Access::Report(*Restarted, Report); if (!Query.IsValid()) return false;
	Complete(Query, 200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	const FString CancelTicket = TEXT("restart-cancelled-before-local-cleanup");
	Consume = Access::Consume(*Restarted, CancelTicket, Steam); if (!Consume.IsValid()) return false;
	const auto CancelState = Access::Recovery(*Restarted, CancelTicket);
	const FString CancelId = CancelState->RequestId, CancelKey = TEXT("restored:") + CancelId;
	const FString CancelPath = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), CancelId + TEXT(".json"));
	Restarted->ObserveDungeonConsumeConnectionLoss(CancelTicket, Steam, Loss);
	Complete(Consume, 503, TEXT("{}")); Access::ExpireRecovery(*Restarted, CancelTicket);
	Query = Access::Evidence(*Restarted, CancelTicket); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	Query = Access::Evidence(*Restarted, CancelTicket); if (!Query.IsValid()) return false;
	const FString Cancellation = FString::Printf(TEXT("{\"state\":\"Cancelled\",\"consumption\":null,\"cancellation\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"ticketHash\":\"%s\",\"expectedSteamId\":\"%s\",\"cancelledAt\":\"2026-10-20T01:03:00Z\"}}"),
		*Session, *Server, *CancelId, *CancelState->TicketHash, *Steam);
	if (!FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*CancelPath, true)) return false;
	ON_SCOPE_EXIT { FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*CancelPath, false); };
	Complete(Query, 200, Cancellation);
	TestTrue(TEXT("Crash window retains observed intent after backend cancellation but failed local cleanup"),
		CancelState->bCancellationVerified && IFileManager::Get().FileExists(*CancelPath));
	Restarted->Deinitialize();
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*CancelPath, false);
	RestoredOutbox = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!RestoredOutbox->Restore()) return false;
	Restarted = NewObject<UHttpWebManager>(RestartedInstance); Access::Configure(*Restarted, RestoredOutbox, Session, Server);
	Access::BindRestoredWorld(*Restarted);
	auto* CancelObserver = NewObject<URPGDungeonMutationTestObserver>();
	Restarted->OnJoinTicketConsumed.AddDynamic(CancelObserver, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	const auto CancelRestored = Access::Recovery(*Restarted, CancelKey);
	if (!TestTrue(TEXT("New manager restores loss without trusting previous volatile cancellation state"),
		CancelRestored && !CancelRestored->bCancellationVerified && CancelRestored->Body.IsEmpty())) return false;
	Query = Access::Evidence(*Restarted, CancelKey); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	Query = Access::Evidence(*Restarted, CancelKey); if (!Query.IsValid()) return false;
	TestTrue(TEXT("Restart reseals the original UUID/hash, never reconsumes a ticket"),
		Query->GetURL().EndsWith(TEXT("/seal-consumption")) && CancelRestored->RequestId == CancelId && CancelRestored->LostAtUtc == Loss);
	Complete(Query, 200, Cancellation);
	TestTrue(TEXT("Verified idempotent cancellation removes only restored file and reopens the empty journals"),
		!Access::Recovery(*Restarted, CancelKey) && !IFileManager::Get().FileExists(*CancelPath)
		&& Restarted->CanDispatchConfiguredDungeonConnectionMutation() && RestoredOutbox->GetPending().IsEmpty());
	TestEqual(TEXT("Restored cancellation emits neither admission nor duplicate failure"), CancelObserver->ConsumeReplies, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConsumeCancellationTest,
	"ProjectRPG.Online.DungeonConsumeCancellationRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConsumeCancellationTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("consume-cancellation-fixture");
	const FString Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"), Steam = TEXT("76561198000000001");
	const FString Backend = TEXT("http://localhost:3000/api"), Ticket = TEXT("cancel-original-ticket"), Other = TEXT("cancel-other-ticket"), Recorded = TEXT("cancel-consumed-ticket");
	const FDateTime Loss(2026, 10, 20, 1, 2, 3, 456);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonConsumeCancellation"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Web = nullptr;
	ON_SCOPE_EXIT { if (Web) Web->Deinitialize(); FCommandLine::Set(*PreviousCommandLine);
		IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!Journal->Restore()) return false;
	Web = NewObject<UHttpWebManager>(NewObject<UGameInstance>()); Access::Configure(*Web, Journal, Session, Server);
	auto* Observer = NewObject<URPGDungeonMutationTestObserver>();
	Web->OnJoinTicketConsumed.AddDynamic(Observer, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	FHttpRequestPtr First = Access::Consume(*Web, Ticket, Steam), Second = Access::Consume(*Web, Other, Steam), Third = Access::Consume(*Web, Recorded, Steam);
	if (!First.IsValid() || !Second.IsValid() || !Third.IsValid()) return false;
	for (const auto& Pair : TArray<TPair<FString, FHttpRequestPtr>>{{Ticket, First}, {Other, Second}, {Recorded, Third}})
	{
		Web->ObserveDungeonConsumeConnectionLoss(Pair.Key, Steam, Loss);
		Complete(Pair.Value, 503, TEXT("{}")); Access::ExpireRecovery(*Web, Pair.Key);
	}
	auto State = Access::Recovery(*Web, Ticket), OtherState = Access::Recovery(*Web, Other), RecordedState = Access::Recovery(*Web, Recorded);
	auto Proof = [&](const auto& Intent)
	{
		return FString::Printf(TEXT("{\"state\":\"Cancelled\",\"consumption\":null,\"cancellation\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"ticketHash\":\"%s\",\"expectedSteamId\":\"%s\",\"cancelledAt\":\"2026-10-20T01:03:00Z\"}}"),
			*Session, *Server, *Intent->RequestId, *Intent->TicketHash, *Steam);
	};
	const FString Valid = Proof(State), Path = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), State->RequestId + TEXT(".json"));
	FString OriginalFile; if (!FFileHelper::LoadFileToString(OriginalFile, *Path)) return false;
	FHttpRequestPtr Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	const TArray<uint8> FrozenBody = Query->GetContent();
	for (int32 Status : {409, 401, 403})
	{
		Complete(Query, Status, TEXT("{}"));
		Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
		TestTrue(TEXT("Generic conflicts and denials do not start sealing"), !State->bSealRequested && Query->GetURL().EndsWith(TEXT("/consumption-evidence")));
	}
	Complete(Query, 200, Valid);
	TestFalse(TEXT("Read-only lookup cannot authorize cancellation"), State->bCancellationVerified);
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"), false);
	TestFalse(TEXT("Failed transport cannot request sealing"), State->bSealRequested);
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	TestTrue(TEXT("Exact unresolved marker requests seal but retains the uncertainty"), State->bSealRequested
		&& Access::Recovery(*Web, Ticket) == State && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	TestFalse(TEXT("Sealing respects the existing thirty-second retry delay"), Access::EvidenceAt(*Web, Ticket, State->NextAttemptAt - 1.0).IsValid());
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	TestTrue(TEXT("Seal uses frozen original scope/peer/hash/UUID body without raw ticket"),
		Query->GetURL() == Backend / TEXT("join-tickets/seal-consumption") && Query->GetContent() == FrozenBody);
	const auto OldCallback = Query->OnProcessRequestComplete(); const FHttpRequestPtr OldQuery = Query;
	TestFalse(TEXT("One intent cannot have overlapping seal requests"), Access::Evidence(*Web, Ticket).IsValid());
	for (int32 Status : {0, 401, 403, 404, 409, 425, 429, 503})
	{
		Complete(Query, Status, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"), Status != 0);
		TestTrue(TEXT("Unknown/old-backend/offline seal preserves file and guard"), !State->bCancellationVerified
			&& Access::Recovery(*Web, Ticket) == State && Journal->GetPending().IsEmpty() && !Web->CanDispatchConfiguredDungeonConnectionMutation());
		Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
		TestTrue(TEXT("Every seal retry keeps the exact original body"), Query->GetContent() == FrozenBody);
	}
	FHttpResponsePtr Good = MakeShared<FResponse, ESPMode::ThreadSafe>(200, Valid);
	OldCallback.ExecuteIfBound(OldQuery, Good, true);
	TestTrue(TEXT("Stale seal callback cannot claim replacement transport"), State->EvidenceRequest == Query && !State->bCancellationVerified);
	for (const FString& Bad : TArray<FString>{TEXT("{}"), FString::ChrN(8193, TEXT('x')),
		Valid.Replace(TEXT("Cancelled"), TEXT("cancelled")), Valid.Replace(*State->RequestId, *OtherState->RequestId),
		Valid.Replace(*State->TicketHash, *OtherState->TicketHash), Valid.Replace(*Steam, TEXT("76561198000000002")),
		Valid.Replace(*Server, *Server.ToUpper()), Valid.Replace(*Session, TEXT("10203040-5060-7080-90a0-b0c0d0e0f099")),
		Valid.Replace(TEXT("\"consumption\":null"), TEXT("\"consumption\":{}")), Valid.Replace(TEXT("01:03:00Z"), TEXT("01:03:00+09:00"))})
	{
		Complete(Query, 200, Bad);
		TestFalse(TEXT("Cross-intent/identity/ambiguous proof cannot cancel"), State->bCancellationVerified);
		Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	}
	Access::Configure(*Web, Journal, Session, Server.ToUpper()); Complete(Query, 200, Valid);
	TestFalse(TEXT("Previous assignment cannot accept a cancellation"), State->bCancellationVerified);
	Access::Configure(*Web, Journal, Session, Server);
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	FString StillPending; TestTrue(TEXT("Every rejected completion preserves original file bytes"),
		FFileHelper::LoadFileToString(StillPending, *Path) && StillPending == OriginalFile);
	// A real filesystem deletion failure, not merely a mocked unavailable journal.
	if (!FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path, true)) return false;
	ON_SCOPE_EXIT { FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path, false); };
	Complete(Query, 200, Valid);
	TestTrue(TEXT("Verified cancellation cannot drop intent before durable deletion succeeds"),
		State->bCancellationVerified && Access::Recovery(*Web, Ticket) == State && Access::ConsumeJournal(*Web)->GetPending().Num() == 3
		&& FFileHelper::LoadFileToString(StillPending, *Path) && StillPending == OriginalFile && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path, false);
	TestFalse(TEXT("Known cancellation retries only file cleanup, not HTTP or consume"), Access::Evidence(*Web, Ticket).IsValid());
	TestTrue(TEXT("Only this intent and its exact file are removed"), !Access::Recovery(*Web, Ticket) && !IFileManager::Get().FileExists(*Path)
		&& Access::Recovery(*Web, Other) == OtherState && Access::Recovery(*Web, Recorded) == RecordedState
		&& Access::ConsumeJournal(*Web)->GetPending().Num() == 2 && Journal->GetPending().IsEmpty());
	TestEqual(TEXT("Proof/cleanup never delivers a second failure or any admission success"), Observer->ConsumeReplies, 3);
	Query = Access::Evidence(*Web, Other); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	Query = Access::Evidence(*Web, Other); if (!Query.IsValid()) return false; Complete(Query, 200, Proof(OtherState));
	TestTrue(TEXT("Another uncertainty still holds the mutation barrier"), !Access::Recovery(*Web, Other) && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	Query = Access::Evidence(*Web, Recorded); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	Query = Access::Evidence(*Web, Recorded); if (!Query.IsValid()) return false;
	const FString Consumed = FString::Printf(TEXT("{\"state\":\"Recorded\",\"cancellation\":null,\"consumption\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":9007199254740993,\"consumedAt\":\"2026-10-20T01:01:00Z\"}}"),
		*Session, *Server, *RecordedState->RequestId, *Character, *Steam);
	Complete(Query, 200, Consumed);
	TestTrue(TEXT("Consume winner hands off exact generation to outbox rather than cancellation"), !Access::Recovery(*Web, Recorded)
		&& !RecordedState->bCancellationVerified && Journal->GetPending().Num() == 1 && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	FRPGDungeonDisconnectionRecord Report; for (const auto& Entry : Journal->GetPending()) Report = Entry.Value;
	TestTrue(TEXT("Original peer and loss UTC survive seal winner race"), Report.ConnectionGeneration == 9007199254740993LL && Report.SteamId == Steam && Report.ObservedAtUtc == Loss);
	Query = Access::Report(*Web, Report); if (!Query.IsValid()) return false; Complete(Query, 200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	TestTrue(TEXT("All exact resolutions reopen mutations only after outbox ACK"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	const FString Last = TEXT("cancel-persistence-guard"), Closing = TEXT("cancel-shutdown");
	First = Access::Consume(*Web, Last, Steam); Second = Access::Consume(*Web, Closing, Steam); if (!First.IsValid() || !Second.IsValid()) return false;
	Web->ObserveDungeonConsumeConnectionLoss(Last, Steam, Loss); Complete(First, 503, TEXT("{}")); Access::ExpireRecovery(*Web, Last);
	Query = Access::Evidence(*Web, Last); if (!Query.IsValid()) return false; Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	Query = Access::Evidence(*Web, Last); if (!Query.IsValid()) return false;
	Access::MarkConsumePersistenceFailed(*Web); Complete(Query, 200, Proof(Access::Recovery(*Web, Last)));
	TestTrue(TEXT("Successful cancellation cannot clear a sticky persistence-failure guard"), !Access::Recovery(*Web, Last)
		&& Access::ConsumeJournal(*Web)->GetPending().IsEmpty() && Journal->GetPending().IsEmpty() && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	Web->ObserveDungeonConsumeConnectionLoss(Closing, Steam, Loss); Complete(Second, 503, TEXT("{}")); Access::ExpireRecovery(*Web, Closing);
	const auto ClosingState = Access::Recovery(*Web, Closing); const FString ClosingProof = Proof(ClosingState);
	Query = Access::Evidence(*Web, Closing); if (!Query.IsValid()) return false; Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	Query = Access::Evidence(*Web, Closing); if (!Query.IsValid()) return false;
	const auto ClosingCallback = Query->OnProcessRequestComplete(); Web->Deinitialize();
	FHttpResponsePtr ClosedReply = MakeShared<FResponse, ESPMode::ThreadSafe>(200, ClosingProof); ClosingCallback.ExecuteIfBound(Query, ClosedReply, true);
	TestTrue(TEXT("Shutdown unbinds seal completion without deleting the durable observation"), !Query->OnProcessRequestComplete().IsBound()
		&& Access::ConsumeJournal(*Web)->GetPending().Contains(ClosingState->RequestId) && Journal->GetPending().IsEmpty());
	TestEqual(TEXT("No historical result admits any of the five expired original peers"), Observer->ConsumeReplies, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConsumeObservationWriteRecoveryTest,
	"ProjectRPG.Online.DungeonConsumeObservationWriteRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConsumeObservationWriteRecoveryTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("consume-write-recovery-fixture");
	const FString Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"), Steam = TEXT("76561198000000001");
	const FString Backend = TEXT("http://localhost:3000/api"), ExistingTicket = TEXT("write-existing"), Ticket = TEXT("write-failed"), OtherTicket = TEXT("write-other-failed");
	const FDateTime Loss(2026, 10, 20, 1, 2, 3, 456);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonConsumeWriteRecovery"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get(); UHttpWebManager* Web = nullptr;
	ON_SCOPE_EXIT { if (Web) Web->Deinitialize(); FCommandLine::Set(*PreviousCommandLine);
		IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!Journal->Restore()) return false;
	Web = NewObject<UHttpWebManager>(NewObject<UGameInstance>()); Access::Configure(*Web, Journal, Session, Server);
	auto* Observer = NewObject<URPGDungeonMutationTestObserver>();
	Web->OnJoinTicketConsumed.AddDynamic(Observer, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	FHttpRequestPtr Existing = Access::Consume(*Web, ExistingTicket, Steam), First = Access::Consume(*Web, Ticket, Steam), Other = Access::Consume(*Web, OtherTicket, Steam);
	if (!Existing.IsValid() || !First.IsValid() || !Other.IsValid()) return false;
	const auto ExistingState = Access::Recovery(*Web, ExistingTicket), State = Access::Recovery(*Web, Ticket), OtherState = Access::Recovery(*Web, OtherTicket);
	Web->ObserveDungeonConsumeConnectionLoss(ExistingTicket, Steam, Loss);
	const FString ExistingPath = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), ExistingState->RequestId + TEXT(".json"));
	FString OriginalExisting; if (!FFileHelper::LoadFileToString(OriginalExisting, *ExistingPath)) return false;
	const FString FailedPath = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), State->RequestId + TEXT(".json"));
	const FString Blocker = FailedPath + TEXT(".tmp"), Marker = FPaths::Combine(Blocker, TEXT("owned-fixture-marker"));
	if (!IFileManager::Get().MakeDirectory(*Blocker, true) || !FFileHelper::SaveStringToFile(TEXT("retained-blocker"), *Marker)) return false;
	Web->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, Loss);
	const double FirstDue = Access::ObservationWriteDue(*Web);
	Web->ObserveDungeonConsumeConnectionLoss(OtherTicket, Steam, Loss + FTimespan::FromSeconds(1));
	Web->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, Loss + FTimespan::FromMinutes(9));
	Web->ObserveDungeonConsumeConnectionLoss(Ticket, TEXT("76561198000000002"), Loss);
	TestTrue(TEXT("Failed observations preserve original peer/first UTC independently of the journal"),
		Access::PendingObservationWrites(*Web) == 2 && State->LostAtUtc == Loss && State->ExpectedSteamId == Steam
		&& OtherState->LostAtUtc == Loss + FTimespan::FromSeconds(1) && Access::ObservationWriteDue(*Web) == FirstDue
		&& !Access::ConsumeJournal(*Web)->IsHealthy() && Access::ConsumeJournal(*Web)->GetPending().Num() == 1);
	TestFalse(TEXT("Unsaved observations cannot permit new admissions or settlement"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Access::RetryObservationWrites(*Web, FirstDue - 0.1);
	TestEqual(TEXT("Early tick cannot reset or advance the first retry"), Access::ObservationWriteDue(*Web), FirstDue);
	Access::RetryObservationWrites(*Web, FirstDue);
	TestEqual(TEXT("First failed retry backs off by three seconds"), Access::ObservationWriteDue(*Web), FirstDue + 3.0);
	TestTrue(TEXT("Recovery preserves invalid directory and its marker"), IFileManager::Get().FileExists(*Marker) && Access::PendingObservationWrites(*Web) == 2);
	const FString Admission = FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":9007199254740993}"), *Session, *Character, *Steam);
	Complete(First, 200, Admission);
	Complete(Existing, 503, TEXT("{}")); Complete(Other, 503, TEXT("{}"));
	TestFalse(TEXT("Original consume cannot be reissued while its observed loss is unsaved"), Access::Retry(*Web, OtherTicket).IsValid());
	Access::ExpireRecovery(*Web, ExistingTicket); Access::ExpireRecovery(*Web, OtherTicket);
	TestTrue(TEXT("Inflight consume winner is retained without admission or premature outbox handoff"),
		State->EvidenceGeneration == 9007199254740993LL && State->bExpired && Journal->GetPending().IsEmpty()
		&& Access::PendingObservationWrites(*Web) == 2 && Observer->ConsumeReplies == 3);
	TestFalse(TEXT("Evidence lookup/handoff waits for the original observation to become durable"), Access::Evidence(*Web, Ticket).IsValid());
	Access::Configure(*Web, Journal, Session, Server.ToUpper());
	Access::RetryObservationWrites(*Web, Access::ObservationWriteDue(*Web));
	TestTrue(TEXT("Changed assignment cannot flush or forget old observations"), Access::PendingObservationWrites(*Web) == 2 && !IFileManager::Get().FileExists(*FailedPath));
	Access::Configure(*Web, Journal, Session, Server);
	if (!IFileManager::Get().Delete(*Marker, true, false, true) || !IFileManager::Get().DeleteDirectory(*Blocker, true, false)) return false;
	if (!FFileHelper::SaveStringToFile(TEXT("{partial-fixture"), *Blocker)) return false;
	Access::RetryObservationWrites(*Web, Access::ObservationWriteDue(*Web));
	FString Malformed;
	TestTrue(TEXT("Partial temporary write is retained and does not become an empty safe journal"),
		FFileHelper::LoadFileToString(Malformed, *Blocker) && Malformed == TEXT("{partial-fixture")
		&& Access::PendingObservationWrites(*Web) == 2 && Access::ConsumeJournal(*Web)->GetPending().Num() == 1);
	if (!IFileManager::Get().Delete(*Blocker, true, false, true)) return false; // Only this fixture's malformed file.
	const double RecoveredAt = Access::ObservationWriteDue(*Web);
	Access::RetryObservationWrites(*Web, RecoveredAt);
	FString CurrentExisting;
	TestTrue(TEXT("Repaired storage flushes all original observations without losing known durable records"),
		Access::PendingObservationWrites(*Web) == 0 && Access::ConsumeJournal(*Web)->IsHealthy()
		&& Access::ConsumeJournal(*Web)->GetPending().Num() == 3
		&& FFileHelper::LoadFileToString(CurrentExisting, *ExistingPath) && CurrentExisting == OriginalExisting);
	const auto* Saved = Access::ConsumeJournal(*Web)->GetPending().Find(State->RequestId);
	const auto* OtherSaved = Access::ConsumeJournal(*Web)->GetPending().Find(OtherState->RequestId);
	if (!TestTrue(TEXT("Recovered UUID/hash/peer/first UTC are exact, not the retry time"), Saved && OtherSaved
		&& Saved->TicketHash == State->TicketHash && Saved->ExpectedSteamId == Steam && Saved->ObservedAtUtc == Loss
		&& OtherSaved->ObservedAtUtc == Loss + FTimespan::FromSeconds(1))) return false;
	TestFalse(TEXT("Successful file writes do not clear pending consumption/outbox barriers"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	TestFalse(TEXT("Known consume result now transfers through the original durable outbox"), Access::Evidence(*Web, Ticket).IsValid());
	TestTrue(TEXT("Only the known intent is handed off after observation persistence"), !Access::Recovery(*Web, Ticket)
		&& Journal->GetPending().Num() == 1 && Access::ConsumeJournal(*Web)->GetPending().Num() == 2);
	for (const FString& ToCancel : {ExistingTicket, OtherTicket})
	{
		const auto Intent = Access::Recovery(*Web, ToCancel);
		FHttpRequestPtr Query = Access::Evidence(*Web, ToCancel); if (!Query.IsValid()) return false;
		Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
		Query = Access::Evidence(*Web, ToCancel); if (!Query.IsValid()) return false;
		const FString Cancellation = FString::Printf(TEXT("{\"state\":\"Cancelled\",\"consumption\":null,\"cancellation\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"ticketHash\":\"%s\",\"expectedSteamId\":\"%s\",\"cancelledAt\":\"2026-10-20T01:03:00Z\"}}"),
			*Session, *Server, *Intent->RequestId, *Intent->TicketHash, *Steam);
		Complete(Query, 200, Cancellation);
	}
	TestTrue(TEXT("Resolving remaining intents still waits for the known disconnection ACK"), Access::ConsumeJournal(*Web)->GetPending().IsEmpty() && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	FRPGDungeonDisconnectionRecord Report; for (const auto& Entry : Journal->GetPending()) Report = Entry.Value;
	TestTrue(TEXT("Final report keeps the exact consumed generation and original loss UTC"), Report.ConnectionGeneration == 9007199254740993LL && Report.ObservedAtUtc == Loss);
	FHttpRequestPtr Query = Access::Report(*Web, Report); if (!Query.IsValid()) return false;
	Complete(Query, 200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	TestTrue(TEXT("Only all durable writes and intent/outbox resolutions reopen the gate"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	const FString Closing = TEXT("write-shutdown"); First = Access::Consume(*Web, Closing, Steam); if (!First.IsValid()) return false;
	Access::MarkConsumePersistenceFailed(*Web); Access::RetryObservationWrites(*Web, RecoveredAt + 100.0);
	TestFalse(TEXT("An empty retry queue cannot clear an unattributed sticky failure"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	const auto ClosingState = Access::Recovery(*Web, Closing);
	const FString ClosingPath = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), ClosingState->RequestId + TEXT(".json"));
	if (!IFileManager::Get().MakeDirectory(*(ClosingPath + TEXT(".tmp")), true)) return false;
	Web->ObserveDungeonConsumeConnectionLoss(Closing, Steam, Loss);
	TestEqual(TEXT("Closing intent has one tracked unflushed observation"), Access::PendingObservationWrites(*Web), 1);
	Web->Deinitialize();
	if (!IFileManager::Get().DeleteDirectory(*(ClosingPath + TEXT(".tmp")), true, false)) return false;
	Access::RetryObservationWrites(*Web, Access::ObservationWriteDue(*Web) + 100.0);
	TestTrue(TEXT("Shutdown cannot flush under a new scope or silently discard an unflushed original observation"),
		Access::PendingObservationWrites(*Web) == 1 && !IFileManager::Get().FileExists(*ClosingPath)
		&& ClosingState->LostAtUtc == Loss && Journal->GetPending().IsEmpty());
	TestEqual(TEXT("No retry or handoff fabricated an admission event"), Observer->ConsumeReplies, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDisconnectionWriteRecoveryTest,
	"ProjectRPG.Online.DungeonDisconnectionWriteRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDisconnectionWriteRecoveryTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("disconnection-write-fixture");
	const FString Backend = TEXT("http://localhost:3000/api");
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonDisconnectionWriteRecovery"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get(); UHttpWebManager* Web = nullptr;
	ON_SCOPE_EXIT { if (Web) Web->Deinitialize(); FCommandLine::Set(*PreviousCommandLine);
		IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!Journal->Restore()) return false;
	Web = NewObject<UHttpWebManager>(NewObject<UGameInstance>()); Access::Configure(*Web, Journal, Session, Server);
	FRPGDungeonDisconnectionRecord Existing;
	Existing.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"); Existing.SteamId = TEXT("76561198000000001");
	Existing.ConnectionGeneration = 9007199254740993LL; Existing.ObservedAtUtc = FDateTime(2026, 10, 20, 1, 2, 3, 456);
	Existing.EncodeBody(Session, Existing.Body);
	FRPGDungeonDisconnectionRecord Lost = Existing, Other = Existing;
	Lost.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f003"); Lost.EncodeBody(Session, Lost.Body);
	Other.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f004"); Other.ObservedAtUtc += FTimespan::FromSeconds(1); Other.EncodeBody(Session, Other.Body);
	auto Observe = [&](const FRPGDungeonDisconnectionRecord& Record)
	{
		return Web->ReportConfiguredDungeonMemberDisconnection(Session, Record.CharacterId, Record.SteamId,
			Record.ConnectionGeneration, Record.ObservedAtUtc);
	};
	Access::ServiceToken(*Web, FString());
	TestTrue(TEXT("Actual observed loss becomes durable even while service credentials are unavailable"), Observe(Existing));
	TestFalse(TEXT("No HTTP request can be created without current credentials"), Access::Report(*Web, Existing).IsValid());
	Access::ServiceToken(*Web, TEXT("ordering-server-fixture"));
	const FString ExistingPath = FPaths::Combine(Directory, Existing.Key() + TEXT(".json"));
	FString OriginalExisting; if (!FFileHelper::LoadFileToString(OriginalExisting, *ExistingPath)) return false;
	const FString LostPath = FPaths::Combine(Directory, Lost.Key() + TEXT(".json")), Blocker = LostPath + TEXT(".tmp");
	const FString Marker = FPaths::Combine(Blocker, TEXT("owned-fixture-marker"));
	if (!IFileManager::Get().MakeDirectory(*Blocker, true) || !FFileHelper::SaveStringToFile(TEXT("retain-blocker"), *Marker)) return false;
	TestFalse(TEXT("Real failed file open cannot claim a durable report"), Observe(Lost));
	const double FirstDue = Access::DisconnectionWriteDue(*Web);
	TestFalse(TEXT("Other actual loss is preserved while storage is unhealthy"), Observe(Other));
	auto Later = Lost; Later.ObservedAtUtc += FTimespan::FromMinutes(9); Later.EncodeBody(Session, Later.Body);
	TestFalse(TEXT("Duplicate lost callback cannot replace the unflushed first loss"), Observe(Later));
	auto WrongPeer = Lost; WrongPeer.SteamId = TEXT("76561198000000002"); WrongPeer.EncodeBody(Session, WrongPeer.Body);
	TestFalse(TEXT("Wrong peer cannot replace a pending same-generation report"), Observe(WrongPeer));
	TestTrue(TEXT("Both unflushed reports block mutations and cannot dispatch HTTP"), Access::PendingDisconnectionWrites(*Web) == 2
		&& !Web->CanDispatchConfiguredDungeonConnectionMutation() && !Access::StoredReport(*Web, Lost.Key()) && !Journal->IsHealthy());
	Access::RetryDisconnectionWrites(*Web, FirstDue - 0.1);
	TestEqual(TEXT("Early tick does not advance the retry"), Access::DisconnectionWriteDue(*Web), FirstDue);
	Access::RetryDisconnectionWrites(*Web, FirstDue);
	TestEqual(TEXT("First failed retry backs off by three seconds"), Access::DisconnectionWriteDue(*Web), FirstDue + 3.0);
	TestTrue(TEXT("Blocked report directory and original durable record remain"), IFileManager::Get().FileExists(*Marker)
		&& Journal->GetPending().FindChecked(Existing.Key()).SameAs(Existing));
	Access::Configure(*Web, Journal, Session, Server.ToUpper());
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web));
	TestEqual(TEXT("Changed assignment cannot consume pending observations"), Access::PendingDisconnectionWrites(*Web), 2);
	Access::Configure(*Web, Journal, Session, Server);
	if (!IFileManager::Get().Delete(*Marker, true, false, true) || !IFileManager::Get().DeleteDirectory(*Blocker, true, false)) return false;
	if (!FFileHelper::SaveStringToFile(TEXT("{partial-owned-fixture"), *Blocker)) return false;
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web));
	FString Malformed;
	TestTrue(TEXT("Partial temporary evidence is never automatically discarded"), Access::PendingDisconnectionWrites(*Web) == 2
		&& FFileHelper::LoadFileToString(Malformed, *Blocker) && Malformed == TEXT("{partial-owned-fixture"));
	if (!IFileManager::Get().Delete(*Blocker, true, false, true)) return false;
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web));
	FString CurrentExisting;
	TestTrue(TEXT("Repair flushes both reports and preserves the original durable bytes"), Access::PendingDisconnectionWrites(*Web) == 0
		&& Journal->GetPending().Num() == 3 && Journal->GetPending().FindChecked(Lost.Key()).SameAs(Lost)
		&& Journal->GetPending().FindChecked(Other.Key()).SameAs(Other)
		&& FFileHelper::LoadFileToString(CurrentExisting, *ExistingPath) && CurrentExisting == OriginalExisting);
	TestFalse(TEXT("Flushed reports still require backend completion before new settlement/admission"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	TestTrue(TEXT("Parent outbox repair preserves the independent consumption journal"), Access::ConsumeJournal(*Web)->IsHealthy() && Access::ConsumeJournal(*Web)->GetPending().IsEmpty());
	TestTrue(TEXT("Caller can acknowledge a previously failed write without refreshing its time"), Observe(Later)
		&& Journal->GetPending().FindChecked(Lost.Key()).SameAs(Lost));
	FHttpRequestPtr Query = Access::Report(*Web, Lost); if (!Query.IsValid()) return false;
	const FTCHARToUTF8 OriginalBody(*Lost.Body);
	TestTrue(TEXT("Recovered report sends the exact original int64/time body"), Query->GetContent().Num() == OriginalBody.Length()
		&& FMemory::Memcmp(Query->GetContent().GetData(), OriginalBody.Get(), OriginalBody.Length()) == 0);
	const auto OldCallback = Query->OnProcessRequestComplete(); const FHttpRequestPtr OldQuery = Query;
	Complete(Query, 503, TEXT("{}")); Query = Access::Report(*Web, Lost); if (!Query.IsValid()) return false;
	FHttpResponsePtr Accepted = MakeShared<FResponse, ESPMode::ThreadSafe>(200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	OldCallback.ExecuteIfBound(OldQuery, Accepted, true);
	TestTrue(TEXT("Old completion cannot remove the replacement request or its file"), Journal->GetPending().Contains(Lost.Key()) && Access::StoredReport(*Web, Lost.Key())->Request == Query);
	Access::Configure(*Web, Journal, Session, Server.ToUpper()); Complete(Query, 200, Accepted->GetContentAsString());
	TestTrue(TEXT("Previous-assignment response cannot acknowledge an outbox file"), Journal->GetPending().Contains(Lost.Key()));
	Access::Configure(*Web, Journal, Session, Server);
	Query = Access::Report(*Web, Lost); if (!Query.IsValid()) return false; Complete(Query, 200, Accepted->GetContentAsString());
	TestFalse(TEXT("One ACK cannot release remaining party loss reports"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Query = Access::Report(*Web, Other); if (!Query.IsValid()) return false; Complete(Query, 409, TEXT("{}"));
	TestTrue(TEXT("Unknown rejection preserves exact durable report"), Journal->GetPending().Contains(Other.Key()));
	Query = Access::Report(*Web, Other); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"dungeon_disconnection_not_available\"}"));
	Query = Access::Report(*Web, Existing); if (!Query.IsValid()) return false; Complete(Query, 200, Accepted->GetContentAsString());
	TestTrue(TEXT("All exact reports resolved reopen the gate after recovered writes"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	Access::MarkDisconnectionPersistenceFailed(*Web);
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web) + 100.0);
	TestFalse(TEXT("Empty tracked write queue cannot clear an unattributed sticky failure"), Web->CanDispatchConfiguredDungeonConnectionMutation());
	FRPGDungeonDisconnectionRecord Closing = Lost; Closing.ConnectionGeneration += 1; Closing.EncodeBody(Session, Closing.Body);
	const FString ClosingPath = FPaths::Combine(Directory, Closing.Key() + TEXT(".json"));
	if (!IFileManager::Get().MakeDirectory(*(ClosingPath + TEXT(".tmp")), true)) return false;
	TestFalse(TEXT("New failure before shutdown remains tracked"), Observe(Closing));
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web) - 0.1);
	Web->Deinitialize();
	if (!IFileManager::Get().DeleteDirectory(*(ClosingPath + TEXT(".tmp")), true, false)) return false;
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web) + 100.0);
	TestTrue(TEXT("Shutdown retains unflushed original reports without writing a new scope"), Access::PendingDisconnectionWrites(*Web) == 1
		&& !IFileManager::Get().FileExists(*ClosingPath) && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonJournalRestoreRecoveryTest,
	"ProjectRPG.Online.DungeonJournalRestoreRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonJournalRestoreRecoveryTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("journal-restore-fixture");
	const FString Backend = TEXT("http://localhost:3000/api"), Steam = TEXT("76561198000000001");
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonJournalRestoreRecovery"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString Directory = FPaths::Combine(Root, TEXT("Both")), Child = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Web = nullptr; UHttpWebManager* ParentOnly = nullptr; UHttpWebManager* Closed = nullptr;
	URPGDungeonMutationTestGameInstance* Instance = nullptr; UWorld* World = nullptr;
	ON_SCOPE_EXIT {
		if (Web) Web->Deinitialize(); if (ParentOnly) ParentOnly->Deinitialize(); if (Closed) Closed->Deinitialize();
		if (Instance) Instance->SetTestWorldContext(nullptr);
		if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		FCommandLine::Set(*PreviousCommandLine); IFileManager::Get().DeleteDirectory(*Root, false, true);
	};
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	FRPGDungeonDisconnectionRecord Loss;
	Loss.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"); Loss.SteamId = Steam;
	Loss.ConnectionGeneration = 9007199254740993LL; Loss.ObservedAtUtc = FDateTime(2026, 10, 20, 1, 2, 3, 456);
	Loss.EncodeBody(Session, Loss.Body);
	FRPGDungeonConsumeLossRecord Intent {TEXT("10203040-5060-7080-90a0-b0c0d0e0f003"), FString::ChrN(64, TEXT('A')), Steam, Loss.ObservedAtUtc};
	auto SeedParent = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	auto SeedChild = MakeShared<FRPGDungeonConsumeJournal>(Child, Backend, Server, Session);
	if (!SeedParent->Restore() || !SeedParent->Persist(Loss) || !SeedChild->Restore() || !SeedChild->Persist(Intent)) return false;
	const FString ParentPath = FPaths::Combine(Directory, Loss.Key() + TEXT(".json"));
	const FString ChildPath = FPaths::Combine(Child, Intent.RequestId + TEXT(".json"));
	const FString ParentPartial = ParentPath + TEXT(".tmp"), ChildPartial = ChildPath + TEXT(".tmp");
	FString ParentBytes, ChildBytes;
	if (!FFileHelper::LoadFileToString(ParentBytes, *ParentPath) || !FFileHelper::LoadFileToString(ChildBytes, *ChildPath)
		|| !FFileHelper::SaveStringToFile(TEXT("partial-parent"), *ParentPartial)
		|| !FFileHelper::SaveStringToFile(TEXT("partial-child"), *ChildPartial)) return false;
	Instance = NewObject<URPGDungeonMutationTestGameInstance>(); Web = NewObject<UHttpWebManager>(Instance);
	auto* Observer = NewObject<URPGDungeonMutationTestObserver>();
	Web->OnJoinTicketConsumed.AddDynamic(Observer, &URPGDungeonMutationTestObserver::ConsumeCompleted);
	Access::InitializeJournals(*Web, Directory, Session, Server);
	TestTrue(TEXT("Both initial failures still arm the production retry ticker"), Access::TickerArmed(*Web)
		&& Access::ParentRestorePending(*Web) && Access::ConsumeRestorePending(*Web));
	Access::StopTicker(*Web); // Control time locally; never dispatch a real HTTP request.
	const auto Parent = Access::DisconnectionJournal(*Web);
	const auto ConsumeJournal = Access::ConsumeJournal(*Web);
	TestTrue(TEXT("Failed startup publishes no partial map or admission"), Access::ReportCount(*Web) == 0
		&& Access::RecoveryCount(*Web) == 0 && Observer->ConsumeReplies == 0 && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	const double FirstDue = Access::RestoreDue(*Web);
	Access::RetryRestores(*Web, FirstDue - 0.1);
	TestEqual(TEXT("Retry waits for the first one-second deadline"), Access::RestoreDue(*Web), FirstDue);
	Access::RetryRestores(*Web, FirstDue);
	TestEqual(TEXT("First failed reinspection backs off three seconds"), Access::RestoreDue(*Web), FirstDue + 3.0);
	FString Unchanged;
	TestTrue(TEXT("Partial files are retained, never treated as an empty healthy journal"),
		FFileHelper::LoadFileToString(Unchanged, *ParentPartial) && Unchanged == TEXT("partial-parent")
		&& FFileHelper::LoadFileToString(Unchanged, *ChildPartial) && Unchanged == TEXT("partial-child")
		&& !Parent->IsHealthy() && !ConsumeJournal->IsHealthy());
	FRPGDungeonDisconnectionRecord Other = Loss;
	Other.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f004"); Other.EncodeBody(Session, Other.Body);
	TestFalse(TEXT("Actual new observation is retained but cannot bypass startup restoration"),
		Web->ReportConfiguredDungeonMemberDisconnection(Session, Other.CharacterId, Steam, Other.ConnectionGeneration, Other.ObservedAtUtc));
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web) + 100.0);
	TestTrue(TEXT("Write retry cannot silently load an unpublished startup journal"), Access::PendingDisconnectionWrites(*Web) == 1
		&& Access::ReportCount(*Web) == 0 && !Parent->IsHealthy());
	// Explicit fixture repair only: production never deletes or rewrites corruption.
	if (!IFileManager::Get().Delete(*ParentPartial) || !FFileHelper::SaveStringToFile(ParentBytes, *ParentPartial)) return false;
	Access::Configure(*Web, Parent, Session, Server.ToUpper());
	Access::RetryRestores(*Web, FirstDue + 3.0);
	TestTrue(TEXT("A changed assignment cannot adopt the original failed startup"), Access::ParentRestorePending(*Web)
		&& Access::RestoreDue(*Web) == FirstDue + 3.0 && IFileManager::Get().FileExists(*ParentPartial));
	Access::Configure(*Web, Parent, Session, Server); Access::ServiceToken(*Web, FString());
	Access::RetryRestores(*Web, FirstDue + 3.0);
	TestEqual(TEXT("Subsequent reinspection uses thirty-second backoff"), Access::RestoreDue(*Web), FirstDue + 33.0);
	TestTrue(TEXT("Parent recovery works without credentials or a world; child remains independently blocked"),
		!Access::ParentRestorePending(*Web) && Access::ConsumeRestorePending(*Web) && Parent->IsHealthy()
		&& !ConsumeJournal->IsHealthy() && Access::ReportCount(*Web) == 1 && Access::RecoveryCount(*Web) == 0
		&& !IFileManager::Get().FileExists(*ParentPartial) && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	TestTrue(TEXT("A complete duplicate temporary file preserves original parent bytes"),
		FFileHelper::LoadFileToString(Unchanged, *ParentPath) && Unchanged == ParentBytes);
	Access::RetryDisconnectionWrites(*Web, Access::DisconnectionWriteDue(*Web) + 100.0);
	TestTrue(TEXT("Buffered real loss is now durable without replacing either journal object"), Access::PendingDisconnectionWrites(*Web) == 0
		&& Parent->GetPending().Num() == 2 && Parent->GetPending().FindChecked(Other.Key()).SameAs(Other)
		&& Access::DisconnectionJournal(*Web) == Parent && Access::ConsumeJournal(*Web) == ConsumeJournal);
	Access::ServiceToken(*Web, TEXT("ordering-server-fixture"));
	FHttpRequestPtr Request = Access::Report(*Web, Loss);
	if (!TestTrue(TEXT("Already restored parent can report while its child awaits storage repair"), Request.IsValid())) return false;
	const auto OriginalReport = Access::StoredReport(*Web, Loss.Key());
	Access::RetryRestores(*Web, FirstDue + 33.0);
	TestTrue(TEXT("Child retry preserves parent's exact in-flight object and callback"),
		Access::StoredReport(*Web, Loss.Key()) == OriginalReport && OriginalReport->Request == Request);
	Complete(Request, 200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	Request = Access::Report(*Web, Other); if (!Request.IsValid()) return false;
	Complete(Request, 200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	TestTrue(TEXT("An empty, ACKed parent cannot release an un-restored child"), Parent->GetPending().IsEmpty()
		&& Access::ReportCount(*Web) == 0 && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	if (!IFileManager::Get().Delete(*ChildPartial)) return false;
	Access::RetryRestores(*Web, Access::RestoreDue(*Web));
	const FString RestoredKey = TEXT("restored:") + Intent.RequestId;
	const auto Recovered = Access::Recovery(*Web, RestoredKey);
	if (!TestTrue(TEXT("Child repair resumes only the original historical intent"), Recovered.IsValid())) return false;
	TestTrue(TEXT("Recovery preserves UUID/hash/peer/first UTC and never restores raw admission body"),
		!Access::ConsumeRestorePending(*Web) && Recovered->RequestId == Intent.RequestId && Recovered->TicketHash == Intent.TicketHash
		&& Recovered->ExpectedSteamId == Steam && Recovered->LostAtUtc == Intent.ObservedAtUtc && Recovered->Body.IsEmpty()
		&& Recovered->bExpired && Recovered->bUncertain && Recovered->bRestoredWorldPending
		&& FFileHelper::LoadFileToString(Unchanged, *ChildPath) && Unchanged == ChildBytes);
	TestTrue(TEXT("Sibling retry does not resurrect acknowledged reports"), Parent->GetPending().IsEmpty() && Access::ReportCount(*Web) == 0);
	TestFalse(TEXT("No original consume can be retried after delayed startup restore"), Access::Retry(*Web, RestoredKey).IsValid());
	TestFalse(TEXT("Evidence cannot start before the restored world binds"), Access::Evidence(*Web, RestoredKey).IsValid());
	World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!World) return false;
	auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World); Context.OwningGameInstance = Instance;
	World->SetGameInstance(Instance); Instance->SetTestWorldContext(&Context);
	Access::BindRestoredWorld(*Web);
	TestTrue(TEXT("Initialization's Game-typed dummy world is not adopted"), Recovered->bRestoredWorldPending);
	World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
	Access::BindRestoredWorld(*Web);
	TestTrue(TEXT("Delayed restore binds the first begun-play world, once"), !Recovered->bRestoredWorldPending && Recovered->Scope.World.Get() == World);
	Request = Access::Evidence(*Web, RestoredKey); if (!Request.IsValid()) return false;
	const FString Evidence = FString::Printf(TEXT("{\"state\":\"Recorded\",\"consumption\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":9007199254740993,\"consumedAt\":\"2026-10-20T01:01:00Z\"}}"),
		*Session, *Server, *Intent.RequestId, *Loss.CharacterId, *Steam);
	Complete(Request, 200, Evidence);
	TestTrue(TEXT("Historical evidence hands off exact original loss, not an admission or new timestamp"),
		!Access::Recovery(*Web, RestoredKey) && ConsumeJournal->GetPending().IsEmpty()
		&& Parent->GetPending().Num() == 1 && Parent->GetPending().FindChecked(Loss.Key()).SameAs(Loss)
		&& Observer->ConsumeReplies == 0 && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	Request = Access::Report(*Web, Loss); if (!Request.IsValid()) return false;
	Complete(Request, 200, TEXT("{\"disconnectedAt\":\"2026-10-20T01:02:03.456Z\"}"));
	Access::RetryRestores(*Web, Access::RestoreDue(*Web) + 100.0);
	TestTrue(TEXT("Resolved startup never reloads or resurrects removed entries; all guards now reopen"),
		Access::RecoveryCount(*Web) == 0 && Access::ReportCount(*Web) == 0 && Web->CanDispatchConfiguredDungeonConnectionMutation());
	Access::MarkConsumePersistenceFailed(*Web); Access::MarkDisconnectionPersistenceFailed(*Web);
	Access::RetryRestores(*Web, Access::RestoreDue(*Web) + 200.0);
	TestFalse(TEXT("Completed restore cannot clear unrelated sticky failures"), Web->CanDispatchConfiguredDungeonConnectionMutation());

	// Opposite case: parent alone failed; the actual production tick retries even
	// before a game world exists, without replacing a healthy child.
	const FString ParentOnlyDirectory = FPaths::Combine(Root, TEXT("ParentOnly"));
	auto ParentOnlySeed = MakeShared<FRPGDungeonDisconnectionJournal>(ParentOnlyDirectory, Backend, Server, Session);
	if (!ParentOnlySeed->Restore() || !ParentOnlySeed->Persist(Loss)) return false;
	const FString ParentOnlyBlocker = FPaths::Combine(ParentOnlyDirectory, TEXT("interrupted.tmp"));
	if (!FFileHelper::SaveStringToFile(TEXT("partial"), *ParentOnlyBlocker)) return false;
	ParentOnly = NewObject<UHttpWebManager>(NewObject<UGameInstance>());
	Access::InitializeJournals(*ParentOnly, ParentOnlyDirectory, Session, Server);
	TestTrue(TEXT("Parent-only failure still starts ticker and loads healthy child"), Access::TickerArmed(*ParentOnly)
		&& Access::ParentRestorePending(*ParentOnly) && !Access::ConsumeRestorePending(*ParentOnly));
	Access::StopTicker(*ParentOnly);
	const auto HealthyChild = Access::ConsumeJournal(*ParentOnly);
	if (!IFileManager::Get().Delete(*ParentOnlyBlocker)) return false;
	Access::ServiceToken(*ParentOnly, FString()); Access::TickRestore(*ParentOnly);
	TestTrue(TEXT("Worldless production tick resumes restored parent without fabricating child losses"),
		!Access::ParentRestorePending(*ParentOnly) && Access::ReportCount(*ParentOnly) == 1
		&& Access::ConsumeJournal(*ParentOnly) == HealthyChild && HealthyChild->GetPending().IsEmpty()
		&& Access::RecoveryCount(*ParentOnly) == 0);

	const FString Unavailable = FPaths::Combine(Root, TEXT("Unavailable"));
	if (!FFileHelper::SaveStringToFile(TEXT("owned-path-blocker"), *Unavailable)) return false;
	Closed = NewObject<UHttpWebManager>(NewObject<UGameInstance>());
	Access::InitializeJournals(*Closed, Unavailable, Session, Server);
	TestTrue(TEXT("Inaccessible root is not restored as empty"), Access::TickerArmed(*Closed)
		&& Access::ParentRestorePending(*Closed) && Access::ConsumeRestorePending(*Closed));
	const auto ClosedParent = Access::DisconnectionJournal(*Closed);
	const auto ClosedChild = Access::ConsumeJournal(*Closed);
	Closed->Deinitialize();
	if (!IFileManager::Get().Delete(*Unavailable)) return false;
	Access::RetryRestores(*Closed, Access::RestoreDue(*Closed) + 100.0);
	TestTrue(TEXT("Shutdown removes ticker and forbids retries or synthetic writes after storage returns"),
		!Access::TickerArmed(*Closed) && !ClosedParent->IsHealthy() && !ClosedChild->IsHealthy()
		&& !IFileManager::Get().DirectoryExists(*Unavailable) && Access::ReportCount(*Closed) == 0 && Access::RecoveryCount(*Closed) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonRecoveryDiagnosticsTest,
	"ProjectRPG.Online.DungeonRecoveryDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonRecoveryDiagnosticsTest::RunTest(const FString&)
{
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	using namespace RPGDisconnectionOrderingTests;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), Server = TEXT("diagnostics-private-server");
	const FString Steam = TEXT("76561198000000001"), Ticket = TEXT("diagnostic-original-secret-ticket"), Other = TEXT("diagnostic-other-secret-ticket");
	const FString Token = TEXT("diagnostic-private-service-token"), Backend = TEXT("http://localhost:3000/api");
	const FDateTime Loss(2026, 10, 21, 1, 2, 3, 456);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonRecoveryDiagnostics"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString PreviousCommandLine = FCommandLine::Get();
	UHttpWebManager* Web = nullptr; UHttpWebManager* Restored = nullptr; FString ReadOnlyPath;
	ON_SCOPE_EXIT {
		if (!ReadOnlyPath.IsEmpty()) FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ReadOnlyPath, false);
		if (Web) Web->Deinitialize(); if (Restored) Restored->Deinitialize();
		FCommandLine::Set(*PreviousCommandLine); IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};
	Web = NewObject<UHttpWebManager>(NewObject<UGameInstance>());
	FCommandLine::Set(TEXT(""));
	TestTrue(TEXT("Diagnostics fail closed outside a server runtime"), Web->BuildConfiguredDungeonRecoveryDiagnostics().Contains(TEXT("server_game_thread_required")));
	FCommandLine::Set(*(PreviousCommandLine + TEXT(" -RPGItemE2EServer")));
	auto Journal = MakeShared<FRPGDungeonDisconnectionJournal>(Directory, Backend, Server, Session);
	if (!Journal->Restore()) return false;
	Access::Configure(*Web, Journal, Session, Server); Access::ServiceToken(*Web, Token);
	TSharedPtr<FJsonObject> Snapshot;
	auto Read = [&](int32 Limit = 32)
	{
		const FString Json = Web->BuildConfiguredDungeonRecoveryDiagnostics(Limit);
		Snapshot.Reset();
		TestTrue(TEXT("Diagnostic JSON is parseable"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Snapshot) && Snapshot.IsValid());
		TestTrue(TEXT("No original ticket, service token, peer, backend address or server string is disclosed"),
			!Json.Contains(Ticket) && !Json.Contains(Other) && !Json.Contains(Token) && !Json.Contains(Steam)
			&& !Json.Contains(Server) && !Json.Contains(Backend));
		return Json;
	};
	auto Intent = [&](const FString& Id) -> TSharedPtr<FJsonObject>
	{
		if (!Snapshot) return nullptr;
		for (const auto& Value : Snapshot->GetArrayField(TEXT("intents")))
			if (Value->AsObject()->GetStringField(TEXT("consumptionRequestId")) == Id) return Value->AsObject();
		return nullptr;
	};
	Read();
	if (!TestTrue(TEXT("Healthy empty startup can be read without opening a request"), Snapshot && Snapshot->GetBoolField(TEXT("mutationsAllowed"))
		&& Snapshot->GetNumberField(TEXT("totalIntents")) == 0)) return false;
	FHttpRequestPtr First = Access::Consume(*Web, Ticket, Steam), Second = Access::Consume(*Web, Other, Steam);
	if (!First.IsValid() || !Second.IsValid()) return false;
	const auto State = Access::Recovery(*Web, Ticket), OtherState = Access::Recovery(*Web, Other);
	Read(1);
	TestTrue(TEXT("Bounded diagnostic output accounts for omitted intents"), Snapshot->GetArrayField(TEXT("intents")).Num() == 1
		&& Snapshot->GetNumberField(TEXT("omittedIntents")) == 1 && Snapshot->GetNumberField(TEXT("activeConsumes")) == 2);
	Read(0); TestEqual(TEXT("Lower limit is safely clamped"), Snapshot->GetArrayField(TEXT("intents")).Num(), 1);
	Read();
	TestTrue(TEXT("Prepared live consumption is distinguished from evidence recovery"), Intent(State->RequestId)->GetStringField(TEXT("stage")) == TEXT("consume_in_flight"));
	Web->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, Loss);
	Complete(First, 503, TEXT("{}")); Access::ExpireRecovery(*Web, Ticket);
	const FString Path = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), State->RequestId + TEXT(".json"));
	FString Original; if (!FFileHelper::LoadFileToString(Original, *Path)) return false;
	FHttpRequestPtr Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	const double Due = State->NextAttemptAt;
	const int32 Retries = State->Retries;
	Read();
	TestTrue(TEXT("Reading an in-flight intent never claims/cancels/retries its request"), Intent(State->RequestId)->GetStringField(TEXT("stage")) == TEXT("evidence_in_flight")
		&& State->EvidenceRequest == Query && Query->OnProcessRequestComplete().IsBound() && State->NextAttemptAt == Due && State->Retries == Retries
		&& Access::ActiveConsume(*Web, Other) == Second && State->LostAtUtc == Loss);
	Complete(Query, 409, TEXT("{}")); Read();
	TestTrue(TEXT("A generic conflict is not evidence absence or permission to seal"), Intent(State->RequestId)->GetStringField(TEXT("lastOutcome")) == TEXT("invalid_response")
		&& !Intent(State->RequestId)->GetBoolField(TEXT("operatorReviewSuggested")) && !State->bSealRequested);
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	Complete(Query, 0, TEXT(""), false); Read();
	TestEqual(TEXT("Transport failure is reported without inferring noncommit"), Intent(State->RequestId)->GetStringField(TEXT("lastOutcome")), FString(TEXT("transport_failure")));
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	Complete(Query, 401, Ticket + Token); Read();
	TestTrue(TEXT("HTTP denial is numeric/sanitized, not an echoed response"), Intent(State->RequestId)->GetStringField(TEXT("lastOutcome")) == TEXT("http_rejected")
		&& Intent(State->RequestId)->GetNumberField(TEXT("lastHttpStatus")) == 401);
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}")); Read();
	TestTrue(TEXT("Missing lookup evidence schedules sealing but does not claim both records are absent"), State->bSealRequested
		&& Intent(State->RequestId)->GetStringField(TEXT("lastOperation")) == TEXT("evidence")
		&& !Intent(State->RequestId)->GetBoolField(TEXT("operatorReviewSuggested")));
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	const auto OldCallback = Query->OnProcessRequestComplete(); const FHttpRequestPtr OldQuery = Query;
	Complete(Query, 409, TEXT("{\"error\":\"join_ticket_consumption_unresolved\"}"));
	const FDateTime CheckedAt = State->LastEvidenceCompletedAtUtc;
	Read();
	TestTrue(TEXT("Authenticated unresolved seal is advisory operator review, never cancellation authority"), Intent(State->RequestId)->GetBoolField(TEXT("operatorReviewSuggested"))
		&& Intent(State->RequestId)->GetStringField(TEXT("lastOperation")) == TEXT("seal")
		&& Intent(State->RequestId)->GetStringField(TEXT("lastOutcome")) == TEXT("backend_unresolved")
		&& Intent(State->RequestId)->GetStringField(TEXT("stage")) == TEXT("seal_retry_pending")
		&& Intent(State->RequestId)->GetStringField(TEXT("observedAtUtc")) == Loss.ToIso8601()
		&& !State->bCancellationVerified && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	FHttpResponsePtr Fake = MakeShared<FResponse, ESPMode::ThreadSafe>(200, TEXT("{}"));
	OldCallback.ExecuteIfBound(OldQuery, Fake, true);
	TestEqual(TEXT("A stale callback cannot rewrite diagnostic completion history"), State->LastEvidenceCompletedAtUtc, CheckedAt);
	FString Unchanged;
	TestTrue(TEXT("Reading never changes the original file or replaces the loss clock"), FFileHelper::LoadFileToString(Unchanged, *Path) && Unchanged == Original && State->LostAtUtc == Loss);
	Access::Configure(*Web, Journal, Session, Server.ToUpper()); Read();
	TestEqual(TEXT("Old assignment is visible without rebinding or discarding its intent"), Intent(State->RequestId)->GetStringField(TEXT("stage")), FString(TEXT("scope_inactive")));
	Access::Configure(*Web, Journal, Session, Server); Access::ServiceToken(*Web, Token);
	const FString Blocker = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"), OtherState->RequestId + TEXT(".json.tmp"));
	if (!IFileManager::Get().MakeDirectory(*Blocker, true)) return false;
	Web->ObserveDungeonConsumeConnectionLoss(Other, Steam, Loss);
	const double WriteDue = Access::ObservationWriteDue(*Web);
	Read();
	TestTrue(TEXT("Known durable observations and failed writes are reported separately without reinspection"),
		Intent(OtherState->RequestId)->GetStringField(TEXT("stage")) == TEXT("observation_write_pending")
		&& Snapshot->GetObjectField(TEXT("consumeJournal"))->GetNumberField(TEXT("pendingWrites")) == 1
		&& !Snapshot->GetObjectField(TEXT("consumeJournal"))->GetBoolField(TEXT("healthy"))
		&& Access::ObservationWriteDue(*Web) == WriteDue && IFileManager::Get().DirectoryExists(*Blocker));
	if (!IFileManager::Get().DeleteDirectory(*Blocker, true, false)) return false; // Owned empty fixture only.
	Access::RetryObservationWrites(*Web, WriteDue);
	if (!TestTrue(TEXT("Only an explicit production write retry recovers storage"), Access::PendingObservationWrites(*Web) == 0)) return false;
	ReadOnlyPath = Path;
	if (!FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path, true)) return false;
	Query = Access::Evidence(*Web, Ticket); if (!Query.IsValid()) return false;
	const FString Cancellation = FString::Printf(TEXT("{\"state\":\"Cancelled\",\"consumption\":null,\"cancellation\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"consumptionRequestId\":\"%s\",\"ticketHash\":\"%s\",\"expectedSteamId\":\"%s\",\"cancelledAt\":\"2026-10-21T01:03:00Z\"}}"),
		*Session, *Server, *State->RequestId, *State->TicketHash, *Steam);
	Complete(Query, 200, Cancellation); Read();
	TestTrue(TEXT("Verified cancellation with failed cleanup is not reported as a resolved intent"), State->bCancellationVerified
		&& Intent(State->RequestId)->GetStringField(TEXT("stage")) == TEXT("cancellation_cleanup_pending")
		&& Intent(State->RequestId)->GetStringField(TEXT("lastOutcome")) == TEXT("cancellation_verified")
		&& IFileManager::Get().FileExists(*Path) && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path, false); Access::Evidence(*Web, Ticket);
	Access::CancelMutations(*Web); Read();
	TestTrue(TEXT("Detached durable observations remain visible but are not rebound by a diagnostic read"), Access::RecoveryCount(*Web) == 0
		&& Intent(OtherState->RequestId)->GetStringField(TEXT("stage")) == TEXT("durable_record_unbound") && !Web->CanDispatchConfiguredDungeonConnectionMutation());
	Restored = NewObject<UHttpWebManager>(NewObject<UGameInstance>()); Access::Configure(*Restored, Journal, Session, Server);
	const FString StartupJson = Restored->BuildConfiguredDungeonRecoveryDiagnostics();
	TestTrue(TEXT("Restored world-wait state remains pending with no false current-world adoption"), StartupJson.Contains(TEXT("awaiting_gameplay_world"))
		&& Access::Recovery(*Restored, TEXT("restored:") + OtherState->RequestId)->bRestoredWorldPending);
	Access::MarkConsumePersistenceFailed(*Web); Access::MarkDisconnectionPersistenceFailed(*Web); Read();
	TestTrue(TEXT("Diagnostic reads never clear unattributed persistence failures"), Snapshot->GetObjectField(TEXT("consumeJournal"))->GetBoolField(TEXT("unattributedFailure"))
		&& Snapshot->GetObjectField(TEXT("disconnectionOutbox"))->GetBoolField(TEXT("unattributedFailure")));
	IConsoleObject* ConsoleObject = IConsoleManager::Get().FindConsoleObject(TEXT("RPG.Dungeon.RecoveryStatus"));
	IConsoleCommand* Command = ConsoleObject ? ConsoleObject->AsCommand() : nullptr;
	if (!TestNotNull(TEXT("Server-local diagnostic command is registered"), Command)) return false;
	FStringOutputDevice Output;
	Command->Execute({}, nullptr, Output);
	TestTrue(TEXT("Console route rejects a missing server world"), Output.Contains(TEXT("server_world_required")));
	Output.Reset(); Command->Execute({TEXT("reset")}, nullptr, Output);
	TestTrue(TEXT("Console exposes no reset/force-clear operation"), Output.Contains(TEXT("invalid_limit")));
	Web->Deinitialize(); TestTrue(TEXT("Shutdown snapshot cannot inspect or restart recovery"), Web->BuildConfiguredDungeonRecoveryDiagnostics().Contains(TEXT("shutdown")));
	return true;
}

namespace RPGConsumeHttpRestart
{
// Explicit integration entrypoint only. This observes a simulated peer loss but
// uses real HTTP, real journal files and a different OS process for restoration.
class FCommand final : public IAutomationLatentCommand
{
public:
	explicit FCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FCommand() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!bInitialized) return Initialize();
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test->AddError(TEXT("HTTP consumption restart recovery timed out"));
			return Finish();
		}
		if (Phase == TEXT("Seed"))
		{
			const auto State = Access::Recovery(*Web, Ticket);
			if (!State || !State->bUncertain || Access::ActiveConsume(*Web, Ticket).IsValid()) return false;
			if (!Test->TestTrue(TEXT("Lost real HTTP response retains the observed original intent"),
				State->LostAtUtc.GetTicks() > 0 && State->ExpectedSteamId == Steam && Observer->ConsumeReplies == 0
				&& Access::ConsumeJournal(*Web)->GetPending().Num() == 1 && !Web->CanDispatchConfiguredDungeonConnectionMutation())) return Finish();
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_CONSUME_HTTP_SEEDED DurableObservation=1 Uncertain=1 AdmissionReplies=0 SimulatedPeerLoss=1"));
			return Finish();
		}
		if (Phase == TEXT("SealLost"))
		{
			if (!IFileManager::Get().FileExists(*SealDropGate)) return false;
			const auto State = Access::Recovery(*Web, RestoredKey);
			if (State && State->EvidenceRequest.IsValid()) return false; // Wait for the actual failed HTTP completion.
			if (!Test->TestTrue(TEXT("Lost committed seal response keeps uncertainty and the original file until proof arrives"), State
				&& State->bSealRequested && !State->bCancellationVerified && State->EvidenceGeneration == 0
				&& State->bExpired && State->bUncertain && State->Body.IsEmpty()
				&& Access::ConsumeJournal(*Web)->GetPending().Num() == 1
				&& Access::DisconnectionJournal(*Web)->GetPending().IsEmpty() && Access::ReportCount(*Web) == 0
				&& Observer->ConsumeReplies == 0 && !Web->CanDispatchConfiguredDungeonConnectionMutation())) return Finish();
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_CONSUME_HTTP_SEAL_LOST JournalRetained=1 Uncertain=1 AdmissionReplies=0 NoDisconnection=1"));
			return Finish();
		}
		if (!Web->CanDispatchConfiguredDungeonConnectionMutation()) return false;
		Test->TestTrue(TEXT("Restored evidence resolves both journals without ever admitting a peer"),
			Access::RecoveryCount(*Web) == 0 && Access::ReportCount(*Web) == 0
			&& Access::ConsumeJournal(*Web)->GetPending().IsEmpty()
			&& Access::DisconnectionJournal(*Web)->GetPending().IsEmpty() && Observer->ConsumeReplies == 0);
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_CONSUME_HTTP_RESUMED JournalsEmpty=1 MutationGateOpen=1 AdmissionReplies=0 RealHttp=1"));
		return Finish();
	}
private:
	using Access = FRPGDungeonDisconnectionOrderingTestAccess;
	bool Initialize()
	{
		bInitialized = true;
		Phase = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RECOVERY_TEST_PHASE"));
		const FString Backend = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RECOVERY_TEST_BACKEND"));
		const FString Server = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_GAME_SERVER_ID"));
		const FString Session = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_DUNGEON_SESSION_ID"));
		const FString Token = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN"));
		const FString Directory = FPaths::ConvertRelativePathToFull(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RECOVERY_TEST_DIRECTORY")));
		// -UserDir intentionally differs between the two processes. This shared,
		// owned journal is under the project, not that per-process Saved directory.
		const FString AllowedRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/Tests/DungeonConsumeHttpRestart")));
		Steam = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RECOVERY_TEST_STEAM"));
		if (!FParse::Param(FCommandLine::Get(), TEXT("RPGConsumeHttpRecovery"))
			|| !FParse::Param(FCommandLine::Get(), TEXT("nosteam"))
			|| (Phase != TEXT("Seed") && Phase != TEXT("Resume") && Phase != TEXT("SealLost"))
			|| (Phase == TEXT("SealLost") && FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RECOVERY_TEST_CANCEL")) != TEXT("1"))
			|| !Backend.StartsWith(TEXT("http://127.0.0.1:")) || !Backend.EndsWith(TEXT("/api"))
			|| !FPaths::IsUnderDirectory(Directory, AllowedRoot) || Server.IsEmpty() || Session.IsEmpty() || Token.IsEmpty())
		{
			Test->AddError(TEXT("Explicit loopback HTTP recovery fixture configuration is required"));
			return true;
		}
		Deadline = FPlatformTime::Seconds() + 180.0;
		SealDropGate = FPaths::Combine(FPaths::GetPath(Directory), TEXT("seal-response-dropped.gate"));
		Instance.Reset(NewObject<URPGDungeonMutationTestGameInstance>());
		Web.Reset(NewObject<UHttpWebManager>(Instance.Get()));
		Access::ConfigureHttp(*Web, Backend, Token);
		Access::InitializeJournals(*Web, Directory, Session, Server);
		Access::ConfigureHttp(*Web, Backend, Token); // InitializeJournals uses the native fixture token.
		Observer.Reset(NewObject<URPGDungeonMutationTestObserver>());
		Web->OnJoinTicketConsumed.AddDynamic(Observer.Get(), &URPGDungeonMutationTestObserver::ConsumeCompleted);
		if (Phase != TEXT("Seed"))
		{
			if (!Test->TestTrue(TEXT("A new OS process restores exactly one historical loss before any game world"),
				Access::RecoveryCount(*Web) == 1 && Access::ConsumeJournal(*Web)->GetPending().Num() == 1
				&& !Web->CanDispatchConfiguredDungeonConnectionMutation())) return Finish();
			const auto& Entry = *Access::ConsumeJournal(*Web)->GetPending().CreateConstIterator();
			RestoredKey = TEXT("restored:") + Entry.Key;
			const auto State = Access::Recovery(*Web, RestoredKey);
			if (!Test->TestTrue(TEXT("Restoration cannot reconsume or query before binding the gameplay world"), State
				&& State->Body.IsEmpty() && State->bExpired && State->bRestoredWorldPending
				&& !State->bSealRequested && !State->bCancellationVerified && State->EvidenceGeneration == 0
				&& !Access::Retry(*Web, RestoredKey).IsValid()
				&& !Access::Evidence(*Web, RestoredKey).IsValid())) return Finish();
		}
		else
		{
			Access::StopTicker(*Web); // First process sends only one original attempt.
			if (!Test->TestTrue(TEXT("Seed starts with empty healthy assigned journals"), Web->CanDispatchConfiguredDungeonConnectionMutation())) return Finish();
		}
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) { Test->AddError(TEXT("Unable to create fixture gameplay world")); return Finish(); }
		auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World); Context.OwningGameInstance = Instance.Get();
		World->SetGameInstance(Instance.Get()); Instance->SetTestWorldContext(&Context);
		World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
		if (Phase == TEXT("Seed"))
		{
			Ticket = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RECOVERY_TEST_TICKET"));
			const FHttpRequestPtr Request = Access::Consume(*Web, Ticket, Steam);
			if (!Request.IsValid()) { Test->AddError(TEXT("Unable to prepare real HTTP consume attempt")); return Finish(); }
			Access::DispatchConsume(*Web, Ticket, Request);
			// Deliberately simulated original-peer socket loss, not a real Iris connection.
			Web->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, FDateTime::UtcNow());
		}
		return false;
	}
	bool Finish() { Cleanup(); return true; }
	void Cleanup()
	{
		if (Web.IsValid()) { Web->Deinitialize(); Web.Reset(); }
		if (Instance.IsValid()) Instance->SetTestWorldContext(nullptr);
		if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr; }
		Observer.Reset(); Instance.Reset(); Ticket.Reset();
		// Journals intentionally survive this process; only the runner owns this GUID directory.
	}
	FAutomationTestBase* Test;
	TStrongObjectPtr<URPGDungeonMutationTestGameInstance> Instance;
	TStrongObjectPtr<UHttpWebManager> Web;
	TStrongObjectPtr<URPGDungeonMutationTestObserver> Observer;
	UWorld* World = nullptr;
	FString Phase, Ticket, Steam, RestoredKey, SealDropGate;
	double Deadline = 0.0;
	bool bInitialized = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConsumeHttpRestartTest,
	"ProjectRPG.Integration.DungeonConsumeHttpRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConsumeHttpRestartTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(RPGConsumeHttpRestart::FCommand(this));
	return true;
}

#endif
