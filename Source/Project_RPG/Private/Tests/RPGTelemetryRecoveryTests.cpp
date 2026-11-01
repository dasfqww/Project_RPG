#if WITH_DEV_AUTOMATION_TESTS

#include "Security/RPGSecurityTelemetrySubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

// New subsystem instances + real isolated files, never Initialize/HTTP/backend receipt polling.
struct FRPGTelemetryRecoveryTestAccess
{
	static void Configure(URPGSecurityTelemetrySubsystem* Telemetry, const FString& Session, const FString& Root)
	{
		Telemetry->bAvailable = true; Telemetry->bPersistToDisk = true;
		Telemetry->DungeonSessionId = Session;
		Telemetry->SpoolPath = FPaths::Combine(Root, TEXT("events.jsonl"));
		Telemetry->DeadLetterPath = FPaths::Combine(Root, TEXT("events.deadletter.jsonl"));
		Telemetry->RevocationPath = FPaths::Combine(Root, TEXT("revocations.json"));
	}
	static FRPGSecurityTelemetryQueue& Queue(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->Queue; }
	static bool SaveQueue(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->PersistQueueSnapshot(); }
	static bool Restore(URPGSecurityTelemetrySubsystem* Telemetry)
	{ return Telemetry->bAvailable = Telemetry->RestorePersistedState(); }
	static bool Revoked(URPGSecurityTelemetrySubsystem* Telemetry, const FString& Character, const FString& Steam)
	{ return Telemetry->IsMemberRevoked(Character, Steam); }
	static bool HasRequest(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->ActiveRequest.IsValid(); }
	static void SetRevocationPath(URPGSecurityTelemetrySubsystem* Telemetry, const FString& Path)
	{ Telemetry->RevocationPath = Path; }
	static void SetDeadLetterPath(URPGSecurityTelemetrySubsystem* Telemetry, const FString& Path)
	{ Telemetry->DeadLetterPath = Path; }
	static bool IsPreserving(const URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->bDeadLetterPending; }
	static FHttpRequestPtr ReserveRequest(URPGSecurityTelemetrySubsystem* Telemetry, int32 Size)
	{
		Telemetry->Queue.BeginBatch(Size);
		auto Request = FHttpModule::Get().CreateRequest();
		Request->OnProcessRequestComplete().BindUObject(Telemetry, &URPGSecurityTelemetrySubsystem::HandleRequestComplete);
		Telemetry->ActiveRequest = Request;
		Telemetry->CurrentBatchAttempt = 1;
		return Request; // Never ProcessRequest: responses are native fixture values.
	}
};

namespace RPGTelemetryRecoveryTests
{
	using Access = FRPGTelemetryRecoveryTestAccess;
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	const FString Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	const FString OtherCharacter = TEXT("10203040-5060-7080-90a0-b0c0d0e0f003");
	const FString ThirdCharacter = TEXT("10203040-5060-7080-90a0-b0c0d0e0f004");
	const FString Steam = TEXT("76561198000000001");
	const FString OtherSteam = TEXT("76561198000000002");
	FRPGSecurityTelemetryEvent Event(const FString& Id = Character, const FString& Account = Steam)
	{
		FRPGSecurityTelemetryEvent Result;
		Result.EventId = FGuid::NewGuid(); Result.CharacterId = Id; Result.SteamId = Account;
		Result.Type = ERPGSecurityViolationType::InvalidCombatHit;
		Result.Severity = ERPGSecurityViolationSeverity::High;
		Result.Score = 1; Result.RiskAfter = 2; Result.ServerTimeSeconds = 3;
		return Result;
	}
	FString Snapshot(const TArray<TPair<FString, FString>>& Members, int32 Version = 1, const FString& Scope = Session)
	{
		TArray<FString> Entries;
		for (const auto& Member : Members)
			Entries.Add(FString::Printf(TEXT("{\"characterId\":\"%s\",\"steamId\":\"%s\"}"), *Member.Key, *Member.Value));
		return FString::Printf(TEXT("{\"schemaVersion\":%d,\"dungeonSessionId\":\"%s\",\"members\":[%s]}"),
			Version, *Scope, *FString::Join(Entries, TEXT(",")));
	}
	struct FFiles
	{
		FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests/TelemetryRecovery"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
		UGameInstance* Instance = NewObject<UGameInstance>();
		FFiles() { IFileManager::Get().MakeDirectory(*Root, true); }
		URPGSecurityTelemetrySubsystem* NewTelemetry() const
		{
			auto* Telemetry = NewObject<URPGSecurityTelemetrySubsystem>(Instance);
			Access::Configure(Telemetry, Session, Root);
			return Telemetry;
		}
		FString Path(const TCHAR* Name) const { return FPaths::Combine(Root, Name); }
		bool Write(const TCHAR* Name, const FString& Contents) const
		{ return FFileHelper::SaveStringToFile(Contents, *Path(Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); }
		FString Read(const TCHAR* Name) const
		{ FString Contents; FFileHelper::LoadFileToString(Contents, *Path(Name)); return Contents; }
	}; // Keep isolated artifacts for inspection; never touch the operational spool directory.
	class FDeliveryResponse final : public IHttpResponse
	{
	public:
		FDeliveryResponse(int32 InStatus, FString InBody) : Status(InStatus), Body(MoveTemp(InBody))
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
		int32 GetResponseCode() const override { return Status; }
		FString GetContentAsString() const override { return Body; }
		FUtf8StringView GetContentAsUtf8StringView() const override
		{ return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	private:
		int32 Status;
		FString Body;
		FString Url = TEXT("http://telemetry-fixture.invalid/api/security/events/batch");
		TArray<uint8> Bytes;
	};
	FHttpResponsePtr DeliveryResponse(int32 Status, FString Body = TEXT("{\"error\":\"fixture_rejection\"}"))
	{ return MakeShared<FDeliveryResponse, ESPMode::ThreadSafe>(Status, MoveTemp(Body)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryStaleSpoolRecoveryTest,
	"ProjectRPG.Security.TelemetryRecovery.StaleSpool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryStaleSpoolRecoveryTest::RunTest(const FString&)
{
	using namespace RPGTelemetryRecoveryTests;
	FFiles Files;
	auto* First = Files.NewTelemetry();
	const auto Removed = Event(), Remaining = Event(OtherCharacter, OtherSteam), ForeignAccount = Event(Character, OtherSteam);
	Access::Queue(First).Enqueue(Removed); Access::Queue(First).Enqueue(Remaining); Access::Queue(First).Enqueue(ForeignAccount);
	TestTrue(TEXT("Original delivery spool persisted"), Access::SaveQueue(First));
	const FString OriginalSpool = Files.Read(TEXT("events.jsonl"));
	TestTrue(TEXT("Tombstone and pending evidence are durably quarantined"), First->QuarantineRevokedDungeonMember(Session, Character, Steam));
	TestEqual(TEXT("Remaining members stay queued"), First->GetQueuedEventCount(), 2);
	const FString DurableSnapshot = Files.Read(TEXT("revocations.json"));
	TestFalse(TEXT("Durable tombstone exists"), DurableSnapshot.IsEmpty());
	TestTrue(TEXT("Removed evidence is kept in dead-letter storage"), Files.Read(TEXT("events.deadletter.jsonl")).Contains(Removed.EventId.ToString(EGuidFormats::DigitsWithHyphensLower)));
	// Simulate the old spool surviving a failed rewrite/process interruption.
	TestTrue(TEXT("Stale spool fixture restored without changing the journal"), Files.Write(TEXT("events.jsonl"), OriginalSpool));
	auto* Restart = Files.NewTelemetry();
	TestTrue(TEXT("Fresh subsystem restores persisted state"), Access::Restore(Restart));
	TestTrue(TEXT("Tombstone restored before delivery recovery"), Access::Revoked(Restart, Character, Steam));
	TestFalse(TEXT("Same character under another account is not revoked"), Access::Revoked(Restart, Character, OtherSteam));
	TestEqual(TEXT("Stale revoked event never enters the delivery batch"), Restart->GetQueuedEventCount(), 2);
	TestTrue(TEXT("Remaining event IDs and order survive recovery"), Access::Queue(Restart).GetEvents().Num() == 2 && Access::Queue(Restart).GetEvents()[0].EventId == Remaining.EventId
		&& Access::Queue(Restart).GetEvents()[1].EventId == ForeignAccount.EventId);
	TestEqual(TEXT("Unchanged restart does not rewrite the tombstone snapshot"), Files.Read(TEXT("revocations.json")), DurableSnapshot);
	TestFalse(TEXT("Recovered spool no longer contains the revoked event"), Files.Read(TEXT("events.jsonl")).Contains(Removed.EventId.ToString(EGuidFormats::DigitsWithHyphensLower)));
	TestFalse(TEXT("Recovery alone does not dispatch HTTP"), Access::HasRequest(Restart));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryPendingSnapshotRecoveryTest,
	"ProjectRPG.Security.TelemetryRecovery.PendingSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryPendingSnapshotRecoveryTest::RunTest(const FString&)
{
	using namespace RPGTelemetryRecoveryTests;
	const FString OldSnapshot = Snapshot({{Character, Steam}});
	const FString NewSnapshot = Snapshot({{OtherCharacter, OtherSteam}});
	FFiles Files;
	auto* Original = Files.NewTelemetry();
	const auto Removed = Event(), AlsoRemoved = Event(OtherCharacter, OtherSteam), Remaining = Event(ThirdCharacter, Steam);
	Access::Queue(Original).Enqueue(Removed); Access::Queue(Original).Enqueue(AlsoRemoved); Access::Queue(Original).Enqueue(Remaining);
	TestTrue(TEXT("Old spool saved"), Access::SaveQueue(Original));
	TestTrue(TEXT("Main snapshot saved"), Files.Write(TEXT("revocations.json"), OldSnapshot));
	TestTrue(TEXT("Unfinished replacement snapshot saved"), Files.Write(TEXT("revocations.json.tmp"), NewSnapshot));
	auto* Restart = Files.NewTelemetry();
	TestTrue(TEXT("Valid main/pending snapshots merge before spool restore"), Access::Restore(Restart));
	TestTrue(TEXT("Older tombstone cannot be lost by a replacement snapshot"), Access::Revoked(Restart, Character, Steam));
	TestTrue(TEXT("New pending tombstone also restored"), Access::Revoked(Restart, OtherCharacter, OtherSteam));
	TestEqual(TEXT("Only the remaining member is deliverable"), Restart->GetQueuedEventCount(), 1);
	TestTrue(TEXT("Remaining event ID preserved"), Access::Queue(Restart).GetEvents().Num() == 1 && Access::Queue(Restart).GetEvents()[0].EventId == Remaining.EventId);
	TestFalse(TEXT("Merged temporary snapshot promoted"), IFileManager::Get().FileExists(*Files.Path(TEXT("revocations.json.tmp"))));
	auto* Again = Files.NewTelemetry();
	TestTrue(TEXT("Promoted snapshot survives a second fresh subsystem"), Access::Restore(Again));
	TestTrue(TEXT("Both tombstones remain durable"), Access::Revoked(Again, Character, Steam) && Access::Revoked(Again, OtherCharacter, OtherSteam));
	FFiles PendingOnly;
	TestTrue(TEXT("Pending-only first write fixture saved"), PendingOnly.Write(TEXT("revocations.json.tmp"), OldSnapshot));
	auto* FromPendingOnly = PendingOnly.NewTelemetry();
	TestTrue(TEXT("Missing main file does not ignore a valid pending snapshot"), Access::Restore(FromPendingOnly));
	TestTrue(TEXT("Pending-only tombstone promoted"), Access::Revoked(FromPendingOnly, Character, Steam)
		&& IFileManager::Get().FileExists(*PendingOnly.Path(TEXT("revocations.json"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryCorruptSnapshotRecoveryTest,
	"ProjectRPG.Security.TelemetryRecovery.InvalidSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryCorruptSnapshotRecoveryTest::RunTest(const FString&)
{
	using namespace RPGTelemetryRecoveryTests;
	TArray<FString> Invalid = {TEXT("{"), FString(), Snapshot({{Character, Steam}}, 2),
		Snapshot({{Character, Steam}}, 1, OtherCharacter), Snapshot({{TEXT("not-a-guid"), Steam}}),
		Snapshot({{TEXT("00000000-0000-0000-0000-000000000000"), Steam}}),
		Snapshot({{Character, TEXT("7656119800000000x")}}), Snapshot({{Character, Steam}, {Character.ToUpper(), Steam}}),
		FString::Printf(TEXT("{\"schemaVersion\":1,\"dungeonSessionId\":\"%s\",\"members\":[null]}"), *Session),
		FString::ChrN(65537, TEXT(' '))};
	TArray<TPair<FString, FString>> TooMany;
	for (int32 Index = 0; Index < 129; ++Index) TooMany.Add({FGuid::NewGuid().ToString(), Steam});
	Invalid.Add(Snapshot(TooMany));
	AddExpectedError(TEXT("Security telemetry revocation recovery failed; delivery disabled and spool preserved."),
		EAutomationExpectedErrorFlags::Contains, Invalid.Num() + 1);
	for (const auto& Contents : Invalid)
	{
		FFiles Files;
		auto* Original = Files.NewTelemetry();
		Access::Queue(Original).Enqueue(Event()); TestTrue(TEXT("Original spool saved"), Access::SaveQueue(Original));
		const FString OriginalSpool = Files.Read(TEXT("events.jsonl"));
		TestTrue(TEXT("Invalid journal fixture saved"), Files.Write(TEXT("revocations.json"), Contents));
		auto* Restart = Files.NewTelemetry();
		TestFalse(TEXT("Invalid journal fails closed"), Access::Restore(Restart));
		TestFalse(TEXT("Telemetry delivery stays disabled"), Restart->IsAvailable());
		TestFalse(TEXT("No POST starts from invalid recovery"), Restart->FlushNow());
		TestEqual(TEXT("Old spool bytes are preserved"), Files.Read(TEXT("events.jsonl")), OriginalSpool);
		TestEqual(TEXT("Invalid journal remains available for diagnosis"), Files.Read(TEXT("revocations.json")), Contents);
		TestEqual(TEXT("Spool is not loaded before valid journal recovery"), Restart->GetQueuedEventCount(), 0);
	}
	FFiles Conflict;
	const FString Main = Snapshot({{Character, Steam}}), Pending = Snapshot({{Character, OtherSteam}});
	TestTrue(TEXT("Main conflict fixture saved"), Conflict.Write(TEXT("revocations.json"), Main));
	TestTrue(TEXT("Pending conflict fixture saved"), Conflict.Write(TEXT("revocations.json.tmp"), Pending));
	auto* Restart = Conflict.NewTelemetry();
	TestFalse(TEXT("Conflicting account cannot replace immutable tombstone"), Access::Restore(Restart));
	TestFalse(TEXT("No partial cache is installed on invalid recovery"), Access::Revoked(Restart, Character, Steam));
	TestEqual(TEXT("Original main snapshot preserved"), Conflict.Read(TEXT("revocations.json")), Main);
	TestEqual(TEXT("Conflicting pending snapshot preserved"), Conflict.Read(TEXT("revocations.json.tmp")), Pending);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetrySnapshotWriteFailureTest,
	"ProjectRPG.Security.TelemetryRecovery.WriteFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetrySnapshotWriteFailureTest::RunTest(const FString&)
{
	using namespace RPGTelemetryRecoveryTests;
	FFiles Files;
	auto* Telemetry = Files.NewTelemetry();
	const auto Removed = Event(), Remaining = Event(OtherCharacter, OtherSteam);
	Access::Queue(Telemetry).Enqueue(Removed); Access::Queue(Telemetry).Enqueue(Remaining);
	TestTrue(TEXT("Original evidence persisted"), Access::SaveQueue(Telemetry));
	const FString OriginalSpool = Files.Read(TEXT("events.jsonl"));
	Access::SetRevocationPath(Telemetry, FString());
	AddExpectedError(TEXT("Revoked member audit quarantine could not be persisted; delivery paused."),
		EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Journal failure cannot acknowledge quarantine"), Telemetry->QuarantineRevokedDungeonMember(Session, Character, Steam));
	TestFalse(TEXT("Journal failure also blocks the next FlushNow"), Telemetry->FlushNow());
	TestFalse(TEXT("No HTTP request was dispatched"), Access::HasRequest(Telemetry));
	TestEqual(TEXT("All original evidence stays queued"), Telemetry->GetQueuedEventCount(), 2);
	TestEqual(TEXT("Original spool is not rewritten before durable tombstone"), Files.Read(TEXT("events.jsonl")), OriginalSpool);
	TestFalse(TEXT("Evidence is not moved before tombstone commit"), IFileManager::Get().FileExists(*Files.Path(TEXT("events.deadletter.jsonl"))));
	Access::SetRevocationPath(Telemetry, Files.Path(TEXT("revocations.json")));
	TestTrue(TEXT("Explicit retry persists the same tombstone then quarantines"), Telemetry->QuarantineRevokedDungeonMember(Session, Character, Steam));
	TestEqual(TEXT("Remaining member evidence stays queued"), Telemetry->GetQueuedEventCount(), 1);
	TestTrue(TEXT("Remaining event identity unchanged"), Access::Queue(Telemetry).GetEvents().Num() == 1 && Access::Queue(Telemetry).GetEvents()[0].EventId == Remaining.EventId);
	auto* Restart = Files.NewTelemetry();
	TestTrue(TEXT("Retried durable tombstone survives fresh recovery"), Access::Restore(Restart));
	TestTrue(TEXT("Recovered identity remains revoked"), Access::Revoked(Restart, Character, Steam));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryRejectedBatchPreservationTest,
	"ProjectRPG.Security.TelemetryRecovery.RejectedBatchPreservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryRejectedBatchPreservationTest::RunTest(const FString&)
{
	using namespace RPGTelemetryRecoveryTests;
	FFiles Files;
	auto* Telemetry = Files.NewTelemetry();
	const auto First = Event(), Second = Event(OtherCharacter, OtherSteam), Later = Event(ThirdCharacter, OtherSteam);
	Access::Queue(Telemetry).Enqueue(First); Access::Queue(Telemetry).Enqueue(Second);
	TestTrue(TEXT("Original mixed batch is durable"), Access::SaveQueue(Telemetry));
	const FString Original = Files.Read(TEXT("events.jsonl"));
	TestTrue(TEXT("A file blocks the dead-letter directory"), Files.Write(TEXT("blocked"), TEXT("blocker")));
	Access::SetDeadLetterPath(Telemetry, FPaths::Combine(Files.Path(TEXT("blocked")), TEXT("events.jsonl")));
	auto Request = Access::ReserveRequest(Telemetry, 2);
	auto Callback = Request->OnProcessRequestComplete();
	AddExpectedError(TEXT("Security telemetry rejected after HTTP 403"), EAutomationExpectedErrorFlags::Contains, 1);
	Callback.Execute(Request, DeliveryResponse(403), true);
	TestTrue(TEXT("Terminal rejection waits for local preservation"), Access::IsPreserving(Telemetry));
	TestTrue(TEXT("Rejected events remain reserved against eviction/retransmission"), Access::Queue(Telemetry).GetReservedCount() == 2);
	TestEqual(TEXT("No event is counted removed before preservation"), Telemetry->GetDroppedEventCount(), int64(0));
	TestEqual(TEXT("Failed preservation leaves original spool byte-for-byte"), Files.Read(TEXT("events.jsonl")), Original);
	TestFalse(TEXT("Terminal request is no longer active"), Access::HasRequest(Telemetry));
	TestFalse(TEXT("FlushNow retries storage, not the rejected POST"), Telemetry->FlushNow());
	TestTrue(TEXT("Repeated write failure retains the same reservation"), Access::IsPreserving(Telemetry)
		&& Access::Queue(Telemetry).GetReservedCount() == 2 && !Access::HasRequest(Telemetry));
	TestTrue(TEXT("Later observations can remain queued behind the reserved batch"), Access::Queue(Telemetry).Enqueue(Later));
	Access::SetDeadLetterPath(Telemetry, Files.Path(TEXT("events.deadletter.jsonl")));
	TestFalse(TEXT("Storage repair does not submit HTTP"), Telemetry->FlushNow());
	TestFalse(TEXT("Successful preservation releases the local pending phase"), Access::IsPreserving(Telemetry));
	TestEqual(TEXT("Only the preserved rejection is removed from delivery"), Telemetry->GetQueuedEventCount(), 1);
	TestEqual(TEXT("Preserved count advances exactly once"), Telemetry->GetDroppedEventCount(), int64(2));
	TestTrue(TEXT("Later observation keeps its exact EventId"), Access::Queue(Telemetry).GetEvents()[0].EventId == Later.EventId);
	const FString DeadLetter = Files.Read(TEXT("events.deadletter.jsonl"));
	TestTrue(TEXT("Both rejected EventIds survive locally"), DeadLetter.Contains(First.EventId.ToString(EGuidFormats::DigitsWithHyphens))
		&& DeadLetter.Contains(Second.EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	TestTrue(TEXT("Remaining spool contains only the later event"), Files.Read(TEXT("events.jsonl")).Contains(Later.EventId.ToString(EGuidFormats::DigitsWithHyphens))
		&& !Files.Read(TEXT("events.jsonl")).Contains(First.EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	Callback.Execute(Request, DeliveryResponse(403), true);
	TestEqual(TEXT("Late original callback cannot append rejected events twice"), Files.Read(TEXT("events.deadletter.jsonl")), DeadLetter);
	TestEqual(TEXT("Late original callback cannot consume later observations"), Telemetry->GetQueuedEventCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryPendingPreservationShutdownTest,
	"ProjectRPG.Security.TelemetryRecovery.PendingPreservationShutdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryPendingPreservationShutdownTest::RunTest(const FString&)
{
	using namespace RPGTelemetryRecoveryTests;
	FFiles Files;
	auto* Telemetry = Files.NewTelemetry();
	const auto Removed = Event(), Peer = Event(OtherCharacter, OtherSteam), Later = Event(ThirdCharacter, OtherSteam);
	Access::Queue(Telemetry).Enqueue(Removed); Access::Queue(Telemetry).Enqueue(Peer);
	TestTrue(TEXT("Mixed evidence saved before terminal response"), Access::SaveQueue(Telemetry));
	Access::SetDeadLetterPath(Telemetry, FString());
	auto Request = Access::ReserveRequest(Telemetry, 2);
	AddExpectedError(TEXT("Security telemetry rejected after HTTP 422"), EAutomationExpectedErrorFlags::Contains, 1);
	Request->OnProcessRequestComplete().Execute(Request, DeliveryResponse(422), true);
	TestTrue(TEXT("Native revocation can persist while rejected preservation is blocked"),
		Telemetry->QuarantineRevokedDungeonMember(Session, Character, Steam));
	TestTrue(TEXT("Revocation must not release a rejected mixed reservation"), Access::IsPreserving(Telemetry)
		&& Access::Queue(Telemetry).GetReservedCount() == 2 && Telemetry->GetQueuedEventCount() == 2);
	Access::Queue(Telemetry).Enqueue(Later);
	Telemetry->Deinitialize();
	TestFalse(TEXT("Shutdown resets only the in-memory pending phase"), Access::IsPreserving(Telemetry));
	// The spool is a sequence of JSON documents, not one SerializeBatch body.
	// Inspect its exact IDs before the real restore parser filters tombstones.
	const FString Persisted = Files.Read(TEXT("events.jsonl"));
	TArray<FString> IdentifierSections;
	Persisted.ParseIntoArray(IdentifierSections, TEXT("\"eventId\""), false);
	TestEqual(TEXT("Shutdown preserves exactly three event records before restart filtering"), IdentifierSections.Num() - 1, 3);
	TestTrue(TEXT("Shutdown preserves the exact session"), Persisted.Contains(Session));
	TestTrue(TEXT("Rejected revoked EventId is durable before filtering"), Persisted.Contains(Removed.EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	TestTrue(TEXT("Rejected peer EventId is durable"), Persisted.Contains(Peer.EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	TestTrue(TEXT("Later observation is durable"), Persisted.Contains(Later.EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	auto* Restart = Files.NewTelemetry();
	TestTrue(TEXT("A fresh subsystem restores all evidence plus the durable revocation"), Access::Restore(Restart));
	TestEqual(TEXT("Existing restoration immediately filters only the durable revoked identity"), Restart->GetQueuedEventCount(), 2);
	TestTrue(TEXT("Restoration preserves the revoked EventId in dead letter"),
		Files.Read(TEXT("events.deadletter.jsonl")).Contains(Removed.EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	TestTrue(TEXT("Original revoked identity still has its tombstone"), Access::Revoked(Restart, Character, Steam));
	TestFalse(TEXT("Fresh recovery does not POST before initial membership validation"), Restart->FlushNow());
	const auto& Remaining = Access::Queue(Restart).GetEvents();
	if (TestEqual(TEXT("Recovery filters only the exact revoked event before delivery"), Remaining.Num(), 2))
	{
		TestEqual(TEXT("Rejected peer EventId survives the fresh subsystem"), Remaining[0].EventId, Peer.EventId);
		TestEqual(TEXT("Later peer EventId and order survive"), Remaining[1].EventId, Later.EventId);
	}
	TestFalse(TEXT("Fresh recovery does not forge an HTTP request or reserve another batch"),
		Access::HasRequest(Restart) || Access::Queue(Restart).HasBatchInFlight());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryDeliveryResponseCompatibilityTest,
	"ProjectRPG.Security.TelemetryRecovery.DeliveryResponseCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryDeliveryResponseCompatibilityTest::RunTest(const FString&)
{
	using namespace RPGTelemetryRecoveryTests;
	for (const int32 Status : {200, 408, 429, 503})
	{
		FFiles Files;
		auto* Telemetry = Files.NewTelemetry();
		const auto First = Event(), Second = Event(OtherCharacter, OtherSteam);
		Access::Queue(Telemetry).Enqueue(First); Access::Queue(Telemetry).Enqueue(Second);
		Access::SaveQueue(Telemetry);
		auto Request = Access::ReserveRequest(Telemetry, 2);
		Request->OnProcessRequestComplete().Execute(Request,
			DeliveryResponse(Status, Status == 200 ? TEXT("{\"acceptedCount\":2,\"duplicateCount\":0}") : TEXT("{}")), true);
		TestFalse(TEXT("ACK and transient failures never enter terminal preservation"), Access::IsPreserving(Telemetry));
		TestFalse(TEXT("Completed responses clear the active request/reservation"), Access::HasRequest(Telemetry) || Access::Queue(Telemetry).HasBatchInFlight());
		TestEqual(TEXT("Valid ACK removes exactly its batch; transient errors keep it"), Telemetry->GetQueuedEventCount(), Status == 200 ? 0 : 2);
		TestEqual(TEXT("ACK/transient errors do not count as permanent drops"), Telemetry->GetDroppedEventCount(), int64(0));
		TestFalse(TEXT("ACK/transient errors do not write a dead letter"), IFileManager::Get().FileExists(*Files.Path(TEXT("events.deadletter.jsonl"))));
		if (Status != 200)
		{
			TestEqual(TEXT("Retry keeps the first exact EventId"), Access::Queue(Telemetry).GetEvents()[0].EventId, First.EventId);
			TestEqual(TEXT("Retry keeps the second exact EventId/order"), Access::Queue(Telemetry).GetEvents()[1].EventId, Second.EventId);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
