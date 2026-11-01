#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Online/RPGDungeonDisconnectionJournal.h"
#include "Online/RPGJoinTicketAdmission.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGJoinTicketAdmissionCodecTest,
	"ProjectRPG.Online.JoinTicketAdmissionCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGJoinTicketAdmissionCodecTest::RunTest(const FString&)
{
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	const FString Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	const FString Steam = TEXT("76561198000000001");
	auto Body = [&](const FString& Generation)
	{
		return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\",\"connectionGeneration\":%s}"),
			*Session, *Character, *Steam, *Generation);
	};
	FRPGJoinTicketAdmission Parsed;
	TestTrue(TEXT("Current numeric generation is accepted"),
		FRPGJoinTicketAdmission::Decode(Body(TEXT("7")), Parsed));
	TestEqual(TEXT("Session preserved"), Parsed.DungeonSessionId, Session);
	TestEqual(TEXT("Character preserved"), Parsed.CharacterId, Character);
	TestEqual(TEXT("Steam identity preserved"), Parsed.SteamId, Steam);
	TestEqual(TEXT("Generation preserved"), Parsed.ConnectionGeneration, int64(7));
	TestTrue(TEXT("Large int64 remains exact"), FRPGJoinTicketAdmission::Decode(
		Body(TEXT("9007199254740993")), Parsed));
	TestEqual(TEXT("No double rounding"), Parsed.ConnectionGeneration, int64(9007199254740993LL));
	TestTrue(TEXT("Maximum int64 stays valid"), FRPGJoinTicketAdmission::Decode(
		Body(TEXT("9223372036854775807")), Parsed));
	TestEqual(TEXT("Maximum int64 is preserved"), Parsed.ConnectionGeneration, MAX_int64);

	for (const FString& Bad : TArray<FString>{TEXT("0"), TEXT("-1"), TEXT("1.5"),
		TEXT("1e0"), TEXT("\"1\""), TEXT("null"), TEXT("true"),
		TEXT("9223372036854775808")})
	{
		TestFalse(FString::Printf(TEXT("Invalid generation %s rejected"), *Bad),
			FRPGJoinTicketAdmission::Decode(Body(Bad), Parsed));
		TestEqual(TEXT("Invalid response clears previous generation"), Parsed.ConnectionGeneration, int64(0));
	}
	TestFalse(TEXT("Missing generation rejected"), FRPGJoinTicketAdmission::Decode(
		FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"steamId\":\"%s\"}"),
			*Session, *Character, *Steam), Parsed));
	TestFalse(TEXT("Invalid Steam identity rejected"), FRPGJoinTicketAdmission::Decode(
		Body(TEXT("1")).Replace(*Steam, TEXT("other-account")), Parsed));
	TestFalse(TEXT("Oversized response rejected"), FRPGJoinTicketAdmission::Decode(
		FString::ChrN(8193, TEXT('a')), Parsed));
	const FString EvidenceJson = FString::Printf(TEXT("{\"state\":\"Recorded\",\"consumption\":%s}"),
		*(Body(TEXT("9007199254740993")).LeftChop(1) +
			TEXT(",\"serverId\":\"codec-server\",\"consumptionRequestId\":\"10203040-5060-7080-90a0-b0c0d0e0f003\",\"consumedAt\":\"2026-10-19T01:01:00Z\"}")));
	FRPGJoinTicketConsumptionEvidence Evidence;
	TestTrue(TEXT("Historical evidence parses UTC Z and exact numeric generation"),
		FRPGJoinTicketConsumptionEvidence::Decode(EvidenceJson, Evidence));
	TestEqual(TEXT("Evidence retains full int64 precision"), Evidence.Consumption.ConnectionGeneration, int64(9007199254740993LL));
	TestFalse(TEXT("Historical envelope is never an admission grant"), FRPGJoinTicketAdmission::Decode(EvidenceJson, Parsed));
	TestFalse(TEXT("Admission grant cannot substitute for historical evidence"),
		FRPGJoinTicketConsumptionEvidence::Decode(Body(TEXT("7")), Evidence));
	TestEqual(TEXT("Invalid evidence clears the previous generation"), Evidence.Consumption.ConnectionGeneration, int64(0));
	const FString RequestId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f003");
	const FString Hash = TEXT("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
	const FString CancelJson = FString::Printf(TEXT("{\"state\":\"Cancelled\",\"consumption\":null,\"cancellation\":{\"dungeonSessionId\":\"%s\",\"serverId\":\"codec-server\",\"consumptionRequestId\":\"%s\",\"ticketHash\":\"%s\",\"expectedSteamId\":\"%s\",\"cancelledAt\":\"2026-10-20T01:01:00+00:00\"}}"),
		*Session, *RequestId, *Hash, *Steam);
	FRPGJoinTicketConsumptionCancellation Cancellation;
	TestTrue(TEXT("Exact nested cancellation proof accepted"), FRPGJoinTicketConsumptionCancellation::Decode(CancelJson, Cancellation));
	TestTrue(TEXT("Cancellation identifies the original intent/hash/peer"), Cancellation.DungeonSessionId == Session
		&& Cancellation.ConsumptionRequestId == RequestId && Cancellation.TicketHash == Hash && Cancellation.ExpectedSteamId == Steam);
	TestFalse(TEXT("Cancellation cannot grant admission"), FRPGJoinTicketAdmission::Decode(CancelJson, Parsed));
	TestFalse(TEXT("Cancellation cannot masquerade as consumed-generation evidence"), FRPGJoinTicketConsumptionEvidence::Decode(CancelJson, Evidence));
	for (const FString& Bad : TArray<FString>{EvidenceJson, TEXT("{}"), FString::ChrN(8193, TEXT('a')),
		CancelJson.Replace(TEXT("Cancelled"), TEXT("cancelled")), CancelJson.Replace(TEXT("\"consumption\":null"), TEXT("\"consumption\":{}")),
		CancelJson.Replace(*Session, *Session.ToUpper()), CancelJson.Replace(*RequestId, TEXT("00000000-0000-0000-0000-000000000000")),
		CancelJson.Replace(*Hash, *Hash.ToLower()), CancelJson.Replace(*Hash, TEXT("wrong-hash")),
		CancelJson.Replace(TEXT("codec-server"), TEXT("invalid/server")), CancelJson.Replace(*Steam, TEXT("not-steam")),
		CancelJson.Replace(TEXT("+00:00"), TEXT("+09:00"))})
	{
		TestFalse(TEXT("Malformed or ambiguous cancellation rejected"), FRPGJoinTicketConsumptionCancellation::Decode(Bad, Cancellation));
		TestTrue(TEXT("Invalid cancellation clears previous proof"), Cancellation.ConsumptionRequestId.IsEmpty() && Cancellation.CancelledAtUtc.GetTicks() == 0);
	}
	TestFalse(TEXT("Recorded evidence with a contradictory cancellation is rejected"),
		FRPGJoinTicketConsumptionEvidence::Decode(EvidenceJson.LeftChop(1) + TEXT(",\"cancellation\":{}}"), Evidence));
	TestTrue(TEXT("Exact backend admission confirmation accepted"),
		FRPGDungeonAdmissionConfirmation::Decode(TEXT("{\"admissionConfirmed\":true}"), 200));
	for (const FString& Bad : TArray<FString>{TEXT("{\"admissionConfirmed\":false}"),
		TEXT("{\"admissionConfirmed\":\"true\"}"), TEXT("{}"), TEXT("not-json")})
		TestFalse(TEXT("Invalid admission confirmation rejected"),
			FRPGDungeonAdmissionConfirmation::Decode(Bad, 200));
	TestFalse(TEXT("Conflict cannot confirm admission"),
		FRPGDungeonAdmissionConfirmation::Decode(TEXT("{\"admissionConfirmed\":true}"), 409));
	FString ConnectionBody;
	TestTrue(TEXT("Server connection request accepts canonical scope"),
		FRPGDungeonConnectionRequest::Encode(Session, Character, Steam,
			9007199254740993LL, ConnectionBody));
	TestEqual(TEXT("Server request preserves exact numeric int64"), ConnectionBody,
		FString(TEXT("{\"steamId\":\"76561198000000001\",\"connectionGeneration\":9007199254740993}")));
	TestFalse(TEXT("Noncanonical session rejected"),
		FRPGDungeonConnectionRequest::Encode(Session.ToUpper(), Character, Steam, 1, ConnectionBody));
	TestTrue(TEXT("Invalid scope clears request body"), ConnectionBody.IsEmpty());
	TestFalse(TEXT("Non-digit Steam identity rejected"),
		FRPGDungeonConnectionRequest::Encode(Session, Character, TEXT("not-steam"), 1, ConnectionBody));
	TestFalse(TEXT("Nonpositive generation rejected"),
		FRPGDungeonConnectionRequest::Encode(Session, Character, Steam, 0, ConnectionBody));
	TestEqual(TEXT("Transport failure retries after one second"),
		FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(1, 0), 1.f);
	TestEqual(TEXT("Server error gets a final delayed retry"),
		FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(2, 503), 3.f);
	TestTrue(TEXT("Third report attempt stops"),
		FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(3, 0) < 0.f);
	TestTrue(TEXT("Authorization failure does not retry"),
		FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(1, 403) < 0.f);
	TestTrue(TEXT("Conflict does not retry"),
		FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(1, 409) < 0.f);
	TestTrue(TEXT("Acknowledged report does not retry"),
		FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(1, 200) < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDisconnectionJournalTest,
	"ProjectRPG.Online.DungeonDisconnectionJournal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonDisconnectionJournalTest::RunTest(const FString&)
{
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	const FString Server = TEXT("disconnection-journal-fixture");
	const FString Backend = TEXT("http://127.0.0.1:3000/api");
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonDisconnectionJournal"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	Record.SteamId = TEXT("76561198000000001");
	Record.ConnectionGeneration = 9007199254740993LL;
	Record.ObservedAtUtc = FDateTime(2026, 10, 19, 1, 2, 3, 456);
	if (!TestTrue(TEXT("Create timestamped original body"), Record.EncodeBody(Session, Record.Body))) return false;
	TestTrue(TEXT("Body includes original UTC observation"), Record.Body.Contains(
		TEXT("\"observedAtUtc\":\"2026-10-19T01:02:03.456Z\"")));
	FString InvalidBody;
	TestFalse(TEXT("New reports cannot omit observation"), FRPGDungeonConnectionRequest::EncodeDisconnection(
		Session, Record.CharacterId, Record.SteamId, Record.ConnectionGeneration, FDateTime(), InvalidBody));
	const FString Path = FPaths::Combine(Directory, Record.Key() + TEXT(".json"));
	FRPGDungeonDisconnectionJournal First(Directory, Backend, Server, Session);
	TestTrue(TEXT("New assignment starts empty"), First.Restore());
	if (!TestTrue(TEXT("Persist before any HTTP"), First.Persist(Record))) return false;
	FString OriginalFile;
	FFileHelper::LoadFileToString(OriginalFile, *Path);
	TestTrue(TEXT("Duplicate report is idempotent"), First.Persist(Record));
	FRPGDungeonDisconnectionRecord Changed = Record;
	Changed.SteamId = TEXT("76561198000000002");
	Changed.EncodeBody(Session, Changed.Body);
	TestFalse(TEXT("Existing generation cannot change identity"), First.Persist(Changed));
	Changed = Record;
	Changed.ObservedAtUtc += FTimespan::FromMinutes(9);
	Changed.EncodeBody(Session, Changed.Body);
	TestFalse(TEXT("Retry cannot move the observation forward"), First.Persist(Changed));
	Changed = Record;
	Changed.ObservedAtUtc += FTimespan::FromSeconds(1);
	TestFalse(TEXT("Envelope timestamp must match frozen HTTP body"), First.Persist(Changed));
	FRPGDungeonDisconnectionJournal Restarted(Directory, Backend, Server, Session);
	if (!TestTrue(TEXT("Process restart restores pending request"), Restarted.Restore())) return false;
	const auto* Restored = Restarted.GetPending().Find(Record.Key());
	if (!TestNotNull(TEXT("Original request retained without an HTTP result"), Restored)) return false;
	TestEqual(TEXT("Large int64 generation stays exact on disk"), Restored->ConnectionGeneration, Record.ConnectionGeneration);
	TestEqual(TEXT("Replay uses frozen HTTP body"), Restored->Body, Record.Body);
	TestEqual(TEXT("Restart preserves original observation"), Restored->ObservedAtUtc, Record.ObservedAtUtc);
	FRPGDungeonDisconnectionJournal OtherBackend(Directory, TEXT("http://127.0.0.1:3001/api"), Server, Session);
	TestFalse(TEXT("Another backend cannot replay this assignment"), OtherBackend.Restore());
	FRPGDungeonDisconnectionJournal OtherServer(Directory, Backend, Server.ToUpper(), Session);
	TestFalse(TEXT("Server scope comparison is case sensitive"), OtherServer.Restore());
	FString AfterScopeFailure;
	FFileHelper::LoadFileToString(AfterScopeFailure, *Path);
	TestEqual(TEXT("Scope rejection preserves original file"), AfterScopeFailure, OriginalFile);
	TestTrue(TEXT("Simulate interruption between flush and rename"),
		IFileManager::Get().Move(*(Path + TEXT(".tmp")), *Path, false, false, false, true));
	FRPGDungeonDisconnectionJournal Interrupted(Directory, Backend, Server, Session);
	TestTrue(TEXT("Fully written temporary report is recovered"), Interrupted.Restore());
	TestTrue(TEXT("Recovered report promoted to final file"), IFileManager::Get().FileExists(*Path));
	FRPGDungeonDisconnectionRecord Other = Record;
	Other.CharacterId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f003");
	Other.ObservedAtUtc = FDateTime();
	Other.EncodeBody(Session, Other.Body);
	TestTrue(TEXT("Legacy v1 member remains independent"), Interrupted.Persist(Other));
	const FString Corrupt = FPaths::Combine(Directory, TEXT("corrupt.json.tmp"));
	FFileHelper::SaveStringToFile(TEXT("interrupted-json"), *Corrupt);
	FRPGDungeonDisconnectionJournal Invalid(Directory, Backend, Server, Session);
	TestFalse(TEXT("Corrupt entry blocks replay without replacing evidence"), Invalid.Restore());
	TestTrue(TEXT("Valid member file survives corrupt restore"), IFileManager::Get().FileExists(*Path));
	IFileManager::Get().Delete(*Corrupt);
	TestTrue(TEXT("Restore after removing only corrupt fixture"), Invalid.Restore());
	TestTrue(TEXT("Resolve exact acknowledged generation"), Invalid.Resolve(Record.Key()));
	FRPGDungeonDisconnectionJournal Final(Directory, Backend, Server, Session);
	TestTrue(TEXT("Restart after acknowledgement"), Final.Restore());
	TestEqual(TEXT("Only other member remains"), Final.GetPending().Num(), 1);
	TestTrue(TEXT("Other member preserved"), Final.GetPending().Contains(Other.Key()));
	const auto* Legacy = Final.GetPending().Find(Other.Key());
	if (TestNotNull(TEXT("Legacy v1 report restored"), Legacy))
	{
		TestEqual(TEXT("Legacy observation is unknown, not restart time"), Legacy->ObservedAtUtc.GetTicks(), int64(0));
		TestEqual(TEXT("Legacy HTTP body preserved exactly"), Legacy->Body, Other.Body);
	}
	TestTrue(TEXT("Backend UTC acknowledgement accepted"), FRPGDungeonDisconnectionJournal::IsAcceptedResponse(
		TEXT("{\"disconnectedAt\":\"2026-10-18T00:00:00+00:00\"}"), 200));
	TestFalse(TEXT("Malformed 200 cannot discard a pending report"),
		FRPGDungeonDisconnectionJournal::IsAcceptedResponse(TEXT("{}"), 200));
	TestFalse(TEXT("Invalid time cannot discard a pending report"),
		FRPGDungeonDisconnectionJournal::IsAcceptedResponse(TEXT("{\"disconnectedAt\":\"invalid\"}"), 200));
	TestFalse(TEXT("Expired service credential preserves pending report"), FRPGDungeonDisconnectionJournal::IsTerminalRejection(TEXT(""), 401));
	TestFalse(TEXT("Rate limiting preserves pending report"), FRPGDungeonDisconnectionJournal::IsTerminalRejection(TEXT(""), 429));
	TestTrue(TEXT("Obsolete generation rejection can resolve report"), FRPGDungeonDisconnectionJournal::IsTerminalRejection(TEXT(""), 403));
	TestFalse(TEXT("A proxy 404 is not a confirmed session rejection"), FRPGDungeonDisconnectionJournal::IsTerminalRejection(TEXT(""), 404));
	TestTrue(TEXT("Exact missing session response resolves report"), FRPGDungeonDisconnectionJournal::IsTerminalRejection(
		TEXT("{\"error\":\"dungeon_session_not_found\"}"), 404));
	TestTrue(TEXT("Outbox retry scope is exact"), Final.MatchesAssignment(Backend, Server, Session)
		&& !Final.MatchesAssignment(Backend, Server.ToUpper(), Session));
	const FString LegacyPath = FPaths::Combine(Directory, Other.Key() + TEXT(".json"));
	FString LegacyFile; if (!FFileHelper::LoadFileToString(LegacyFile, *LegacyPath)) return false;
	if (!IFileManager::Get().Delete(*LegacyPath, true, false, true)) return false;
	TestTrue(TEXT("Missing known durable outbox entry cannot be treated as an empty success"), !Final.RecoverForRetry()
		&& Final.GetPending().FindChecked(Other.Key()).SameAs(Other));
	const FString ChangedLegacy = LegacyFile.Replace(*Other.SteamId, TEXT("76561198000000002"));
	if (!FFileHelper::SaveStringToFile(ChangedLegacy, *LegacyPath)) return false;
	TestTrue(TEXT("Well-formed changed peer cannot replace a previously durable legacy record"), !Final.RecoverForRetry()
		&& Final.GetPending().FindChecked(Other.Key()).SameAs(Other));
	FString RetainedLegacy;
	TestTrue(TEXT("Rejected retry leaves conflicting disk evidence untouched"), FFileHelper::LoadFileToString(RetainedLegacy, *LegacyPath) && RetainedLegacy == ChangedLegacy);
	if (!FFileHelper::SaveStringToFile(LegacyFile, *LegacyPath)) return false;
	TestTrue(TEXT("Repair of only the owned fixture permits outbox recovery"), Final.RecoverForRetry());
	if (!IFileManager::Get().Move(*(LegacyPath + TEXT(".tmp")), *LegacyPath, false, false, false, true)) return false;
	TestTrue(TEXT("Validated legacy temporary report is promoted without inventing UTC"), Final.RecoverForRetry()
		&& Final.GetPending().FindChecked(Other.Key()).ObservedAtUtc.GetTicks() == 0);
	const FString Child = FPaths::Combine(Directory, TEXT("ConsumeRecoveries"));
	if (!IFileManager::Get().MakeDirectory(*Child, true) || !FFileHelper::SaveStringToFile(TEXT("owned-child-fixture"), *FPaths::Combine(Child, TEXT("fixture")))) return false;
	TestTrue(TEXT("Expected independent consumption child does not poison the outbox"), Final.RecoverForRetry());
	const FString BlockedWrite = FPaths::Combine(Directory, Record.Key() + TEXT(".json.tmp"));
	if (!IFileManager::Get().MakeDirectory(*BlockedWrite, true)) return false;
	TestTrue(TEXT("A report path blocked by a directory is retained, not silently ignored"), !Final.RecoverForRetry()
		&& IFileManager::Get().DirectoryExists(*BlockedWrite) && Final.GetPending().FindChecked(Other.Key()).SameAs(Other));
	if (!IFileManager::Get().DeleteDirectory(*BlockedWrite, true, false)) return false;
	TestTrue(TEXT("Known durable record remains after directory repair"), Final.RecoverForRetry());
	const FString ParentBlocker = FPaths::Combine(Directory, TEXT("owned-parent-blocker"));
	if (!FFileHelper::SaveStringToFile(TEXT("preserved-fixture"), *ParentBlocker)) return false;
	FRPGDungeonDisconnectionJournal Unreachable(FPaths::Combine(ParentBlocker, TEXT("child")), Backend, Server, Session);
	TestFalse(TEXT("An unreachable outbox is never a healthy empty one"), Unreachable.Restore());
	return true;
}
#endif
