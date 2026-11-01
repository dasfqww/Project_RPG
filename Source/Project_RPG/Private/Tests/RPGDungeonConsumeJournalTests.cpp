#if WITH_DEV_AUTOMATION_TESTS

#include "Online/RPGDungeonConsumeJournal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConsumeJournalTest,
	"ProjectRPG.Online.DungeonConsumeLossJournal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConsumeJournalTest::RunTest(const FString&)
{
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Tests/DungeonConsumeLossJournal"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	const FString Backend = TEXT("http://consume-fixture.invalid/api"), Server = TEXT("consume-journal-fixture");
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	FRPGDungeonConsumeLossRecord Record {TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"),
		FString::ChrN(64, TEXT('A')),
		TEXT("76561198000000001"), FDateTime(2026, 10, 20, 1, 2, 3, 456)};
	// Fixed-length fixture hash; no source ticket or credential ever enters this storage API.
	FRPGDungeonConsumeJournal Journal(Directory, Backend, Server, Session);
	if (!TestTrue(TEXT("Fresh empty journal restores"), Journal.Restore())) return false;
	const FString BlockedParent = FPaths::Combine(Directory, TEXT("blocked-parent-fixture"));
	if (!FFileHelper::SaveStringToFile(TEXT("fixture-owned-blocker"), *BlockedParent)) return false;
	FRPGDungeonConsumeJournal Unreachable(FPaths::Combine(BlockedParent, TEXT("child")), Backend, Server, Session);
	TestFalse(TEXT("An unreachable directory is not an empty safe journal"), Unreachable.Restore());
	FString PreservedBlocker;
	TestTrue(TEXT("Failed restore preserves the blocking fixture file"),
		FFileHelper::LoadFileToString(PreservedBlocker, *BlockedParent) && PreservedBlocker == TEXT("fixture-owned-blocker"));
	if (!IFileManager::Get().Delete(*BlockedParent, true, false, true)) return false;
	for (int32 Bad : {0, 1, 2, 3})
	{
		auto Invalid = Record;
		if (Bad == 0) Invalid.RequestId = TEXT("../not-a-request");
		if (Bad == 1) Invalid.TicketHash = Record.TicketHash.ToLower();
		if (Bad == 2) Invalid.ExpectedSteamId.Reset();
		if (Bad == 3) Invalid.ObservedAtUtc = FDateTime();
		TestFalse(TEXT("Missing original identity/time and unsafe keys cannot be persisted"), Journal.Persist(Invalid));
	}
	if (!TestTrue(TEXT("First observed loss flushed to disk"), Journal.Persist(Record))) return false;
	const FString Path = FPaths::Combine(Directory, Record.RequestId + TEXT(".json"));
	FString Original;
	if (!FFileHelper::LoadFileToString(Original, *Path)) return false;
	TestTrue(TEXT("Stored fields include scope/nonce/hash/peer/first UTC only"),
		Original.Contains(Record.RequestId) && Original.Contains(Record.TicketHash)
		&& !Original.Contains(TEXT("joinTicket")) && !Original.Contains(TEXT("Authorization")));
	TestTrue(TEXT("Duplicate first observation is idempotent"), Journal.Persist(Record));
	auto Changed = Record; Changed.ObservedAtUtc += FTimespan::FromMinutes(1);
	TestFalse(TEXT("Later retry cannot refresh the original loss clock"), Journal.Persist(Changed));
	Changed = Record; Changed.TicketHash = FString::ChrN(64, TEXT('B'));
	TestFalse(TEXT("Nonce cannot be rebound to a different ticket"), Journal.Persist(Changed));
	Changed = Record; Changed.ExpectedSteamId = TEXT("76561198000000002");
	TestFalse(TEXT("Nonce cannot be rebound to a different peer"), Journal.Persist(Changed));
	FRPGDungeonConsumeJournal OtherScope(Directory, Backend, Server.ToUpper(), Session);
	TestFalse(TEXT("Another assignment cannot restore the observations"), OtherScope.Restore());
	TestTrue(TEXT("Rejected restore never partly exposes records"), OtherScope.GetPending().IsEmpty());
	FRPGDungeonConsumeJournal Restored(Directory, Backend, Server, Session);
	TestTrue(TEXT("New journal instance restores exactly the original observation"),
		Restored.Restore() && Restored.GetPending().FindChecked(Record.RequestId).SameAs(Record));
	// Simulate a fully flushed temporary write interrupted before rename.
	if (!TestTrue(TEXT("Move only our fixture record to its temporary name"),
		IFileManager::Get().Move(*(Path + TEXT(".tmp")), *Path, false, false, false, true))) return false;
	FRPGDungeonConsumeJournal Interrupted(Directory, Backend, Server, Session);
	TestTrue(TEXT("Interrupted flushed write is validated/promoted"), Interrupted.Restore());
	TestTrue(TEXT("Same main/tmp copies merge idempotently"), FFileHelper::SaveStringToFile(Original, *(Path + TEXT(".tmp"))));
	FRPGDungeonConsumeJournal Duplicate(Directory, Backend, Server, Session);
	TestTrue(TEXT("Identical temporary copy is safe"), Duplicate.Restore());
	const FString Conflict = Original.Replace(*Record.ExpectedSteamId, TEXT("76561198000000002"));
	if (!FFileHelper::SaveStringToFile(Conflict, *(Path + TEXT(".tmp")))) return false;
	FRPGDungeonConsumeJournal Conflicting(Directory, Backend, Server, Session);
	TestFalse(TEXT("Conflicting main/tmp copies fail closed"), Conflicting.Restore());
	TestTrue(TEXT("Conflict leaves no partial records"), Conflicting.GetPending().IsEmpty());
	FString Retained;
	TestTrue(TEXT("Original main file is preserved after failure"), FFileHelper::LoadFileToString(Retained, *Path) && Retained == Original);
	// Repair only this test-created temporary file, then exercise exact terminal cleanup.
	if (!FFileHelper::SaveStringToFile(Original, *(Path + TEXT(".tmp")))) return false;
	FRPGDungeonConsumeJournal Resolved(Directory, Backend, Server, Session);
	const FString CorruptPath = FPaths::Combine(Directory, TEXT("corrupt-fixture.json"));
	if (!FFileHelper::SaveStringToFile(TEXT("{"), *CorruptPath)) return false;
	FRPGDungeonConsumeJournal Corrupt(Directory, Backend, Server, Session);
	TestFalse(TEXT("One malformed file rejects the entire restore without partial application"), Corrupt.Restore());
	TestTrue(TEXT("Malformed file is retained for repair"), Corrupt.GetPending().IsEmpty() && IFileManager::Get().FileExists(*CorruptPath));
	if (!IFileManager::Get().Delete(*CorruptPath, true, false, true)) return false;
	TestTrue(TEXT("Validated record can be resolved only by its own nonce"), Resolved.Restore() && Resolved.Resolve(Record.RequestId));
	TestTrue(TEXT("Resolved journal is empty"), Resolved.GetPending().IsEmpty());
	FRPGDungeonConsumeJournal Retry(Directory, Backend, Server, Session);
	if (!Retry.Restore() || !Retry.Persist(Record)) return false;
	TestTrue(TEXT("Retry writer accepts only its exact assignment"), Retry.MatchesAssignment(Backend, Server, Session)
		&& !Retry.MatchesAssignment(Backend, Server.ToUpper(), Session));
	// Runtime recovery must retain the last known durable set, not reset to a
	// newly empty or changed directory as startup restoration legitimately can.
	if (!IFileManager::Get().Delete(*Path, true, false, true)) return false;
	TestTrue(TEXT("Missing durable observation cannot reopen an empty journal"), !Retry.RecoverForRetry()
		&& !Retry.IsHealthy() && Retry.GetPending().FindChecked(Record.RequestId).SameAs(Record));
	if (!FFileHelper::SaveStringToFile(Conflict, *Path)) return false;
	TestTrue(TEXT("Valid but changed disk identity cannot replace known durable metadata"), !Retry.RecoverForRetry()
		&& Retry.GetPending().FindChecked(Record.RequestId).SameAs(Record));
	FString UnchangedConflict;
	TestTrue(TEXT("Rejected recovery does not rewrite conflicting disk evidence"), FFileHelper::LoadFileToString(UnchangedConflict, *Path) && UnchangedConflict == Conflict);
	if (!FFileHelper::SaveStringToFile(Original, *Path)) return false;
	TestTrue(TEXT("Explicit fixture repair permits exact recovery"), Retry.RecoverForRetry() && Retry.IsHealthy());
	if (!IFileManager::Get().Move(*(Path + TEXT(".tmp")), *Path, false, false, false, true)) return false;
	TestTrue(TEXT("Recovery promotes an unchanged fully flushed temporary observation"), Retry.RecoverForRetry()
		&& IFileManager::Get().FileExists(*Path) && Retry.GetPending().FindChecked(Record.RequestId).SameAs(Record));
	TestTrue(TEXT("Recovered record resolves normally"), Retry.Resolve(Record.RequestId));
	return true;
}

#endif
