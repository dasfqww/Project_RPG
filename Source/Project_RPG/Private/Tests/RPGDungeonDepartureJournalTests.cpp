#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonDepartureJournal.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

struct FRPGDungeonDepartureJournalTestAccess
{
	static void Bind(URPGDungeonSessionSubsystem* Flow, UHttpWebManager* Web, const FString& Directory)
	{
		Flow->WebManager = Web; Flow->DepartureRecoveryDirectory = Directory;
	}
	static bool Login(UHttpWebManager* Web, const FString& Account = TEXT("76561198000000001"))
	{
		Web->ApiUrl = TEXT("http://departure-fixture.invalid/api");
		const auto Issued = FDateTime::UtcNow(); const double Now = FPlatformTime::Seconds();
		return Web->PlayerAuth.AcceptLogin(TEXT("departure-memory-credential-fixture"), Account,
			(Issued + FTimespan::FromMinutes(5)).ToIso8601(), Issued.ToIso8601(), Now, Now);
	}
	static FRPGDungeonDepartureCommand Command(URPGDungeonSessionSubsystem* Flow) { return Flow->GetPendingDepartureCommand(); }
	static void Read(URPGDungeonSessionSubsystem* Flow, const FRPGDungeonSession& Session)
	{
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::Resume;
		Flow->HandleDungeonSessionUpdated(Session, true);
	}
	static bool Restore(URPGDungeonSessionSubsystem* Flow) { return Flow->RestoreDepartureCommand(); }
	static void SetEndpoint(UHttpWebManager* Web, const FString& Endpoint) { Web->ApiUrl = Endpoint; }
};

namespace
{
	FRPGDungeonDepartureCommand MakeCommand(bool bCancel = false)
	{
		return { TEXT("http://departure-fixture.invalid/api"), TEXT("76561198000000001"),
			TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"), TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"),
			TEXT("10203040-5060-7080-90a0-b0c0d0e0f003"), 10, bCancel };
	}
	FString TestDirectory()
	{
		// Preserve only this run's bounded fixtures for diagnostics; never touch existing player records.
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests/DepartureJournal"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}
	FRPGDungeonSession Snapshot(const FRPGDungeonDepartureCommand& Command, bool bQueued, const FString& State = TEXT("Waiting"))
	{
		FRPGDungeonSession Session; Session.DungeonSessionId = Command.SessionId; Session.State = State;
		Session.LeaderCharacterId = Command.CharacterId; Session.LobbyRevision = bQueued ? 11 : 10;
		FRPGDungeonSessionMember Member; Member.CharacterId = Command.CharacterId; Session.Members.Add(Member);
		if (bQueued) { Session.DepartureRequestId = Command.RequestId; Session.DepartureRequestRevision = 10; Session.DepartureRequestedAt = TEXT("confirmed"); }
		return Session;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDepartureJournalTest, "ProjectRPG.Online.DungeonDepartureJournal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDepartureJournalTest::RunTest(const FString&)
{
	const auto Record = MakeCommand(); const FString Directory = TestDirectory();
	FRPGDungeonDepartureJournal Journal(Directory, Record.BackendUrl, Record.SteamId, Record.CharacterId);
	FRPGDungeonDepartureCommand Loaded;
	TestTrue(TEXT("Empty scoped storage is missing, not a restored command"), Journal.Load(Loaded) == ERPGDepartureJournalLoad::Missing);
	if (!TestTrue(TEXT("Original command is flushed before dispatch"), Journal.Persist(Record))) return false;
	TestTrue(TEXT("Exact persist retry is idempotent"), Journal.Persist(Record));
	auto Other = Record; Other.RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	TestFalse(TEXT("New request cannot overwrite pending identity"), Journal.Persist(Other));
	TestFalse(TEXT("Mismatched resolution cannot erase the command"), Journal.Resolve(Other));
	Other = Record; Other.bCancel = true;
	TestFalse(TEXT("Opposite intent cannot overwrite pending identity"), Journal.Persist(Other));
	Other = Record; Other.LobbyRevision = 11;
	TestFalse(TEXT("A retry cannot refresh the original base revision"), Journal.Persist(Other));
	FRPGDungeonDepartureJournal Reopened(Directory, Record.BackendUrl, Record.SteamId, Record.CharacterId);
	TestTrue(TEXT("A fresh journal restores the identical tuple"), Reopened.Load(Loaded) == ERPGDepartureJournalLoad::Found && Loaded.SameAs(Record));
	FRPGDungeonDepartureJournal OtherAccount(Directory, Record.BackendUrl, TEXT("76561198000000002"), Record.CharacterId);
	TestTrue(TEXT("Different account sees no record"), OtherAccount.Load(Loaded) == ERPGDepartureJournalLoad::Missing);
	FRPGDungeonDepartureJournal OtherBackend(Directory, TEXT("http://departure-fixture.invalid/API"), Record.SteamId, Record.CharacterId);
	TestTrue(TEXT("Different endpoint path has independent storage"), OtherBackend.Load(Loaded) == ERPGDepartureJournalLoad::Missing);
	FString Contents; if (!FFileHelper::LoadFileToString(Contents, *Journal.GetPath())) return false;
	TestTrue(TEXT("Only noncredential command fields are persisted"), !Contents.Contains(TEXT("credential"))
		&& !Contents.Contains(TEXT("accessToken")) && !Contents.Contains(TEXT("Authorization")) && !Contents.Contains(TEXT("joinTicket")));
	const FString Temporary = Journal.GetPath() + TEXT(".tmp");
	if (!IFileManager::Get().Move(*Temporary, *Journal.GetPath(), false, false, false, true)) return false;
	TestTrue(TEXT("A complete interrupted temporary write recovers the same identity"), Reopened.Load(Loaded) == ERPGDepartureJournalLoad::Found && Loaded.SameAs(Record));
	if (!FFileHelper::SaveStringToFile(TEXT("{partial"), *Temporary)) return false;
	TestTrue(TEXT("Partial temporary write fails closed without erasing the final record"), Reopened.Load(Loaded) == ERPGDepartureJournalLoad::Error
		&& IFileManager::Get().FileExists(*Journal.GetPath()));
	if (!IFileManager::Get().Delete(*Temporary, true, false, true)) return false;
	TestTrue(TEXT("Only exact confirmed command can be removed"), Reopened.Resolve(Record));
	TestTrue(TEXT("Resolution stays removed after reopening"), Journal.Load(Loaded) == ERPGDepartureJournalLoad::Missing);
	if (!FFileHelper::SaveStringToFile(TEXT("fixture-owned-blocker"), *Journal.GetPath())) return false;
	FRPGDungeonDepartureJournal Blocked(FPaths::Combine(Journal.GetPath(), TEXT("child")), Record.BackendUrl, Record.SteamId, Record.CharacterId);
	TestTrue(TEXT("Unreachable directory is an error, not empty evidence"), Blocked.Load(Loaded) == ERPGDepartureJournalLoad::Error);
	TestFalse(TEXT("Blocked storage never accepts a command for dispatch"), Blocked.Persist(Record));
	UGameInstance* Instance = NewObject<UGameInstance>();
	auto* Web = NewObject<UHttpWebManager>(Instance); auto* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	FRPGDungeonDepartureJournalTestAccess::Bind(Flow, Web, Directory);
	if (!FRPGDungeonDepartureJournalTestAccess::Login(Web)) return false;
	TestFalse(TEXT("Malformed local record blocks selection recovery"), Flow->SelectCharacter(Record.CharacterId));
	TestTrue(TEXT("Storage error is exposed separately from an authenticated receipt"), Flow->HasDepartureRecoveryError());
	TestFalse(TEXT("Corrupt storage cannot dispatch a fresh create"), Flow->CreateDungeonSession(TEXT("Dungeon.RaidSwordMan"), TEXT("Normal")));
	TestFalse(TEXT("No HTTP operation was started on storage failure"), Flow->IsRequestInFlight());
	if (!IFileManager::Get().Delete(*Journal.GetPath(), true, false, true)) return false;
	TestTrue(TEXT("After local repair selection retries storage recovery"), Flow->SelectCharacter(Record.CharacterId));
	TestFalse(TEXT("Healthy storage clears the error without fabricating a command"), Flow->HasDepartureRecoveryError() || Flow->HasUnresolvedDeparture());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDepartureRestartRecoveryTest, "ProjectRPG.Online.DungeonDepartureRestartRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDepartureRestartRecoveryTest::RunTest(const FString&)
{
	for (bool bCancel : {false, true})
	{
		const auto Record = MakeCommand(bCancel); const FString Directory = TestDirectory();
		FRPGDungeonDepartureJournal Journal(Directory, Record.BackendUrl, Record.SteamId, Record.CharacterId);
		if (!Journal.Persist(Record)) return false;
		UGameInstance* Instance = NewObject<UGameInstance>();
		auto* Web = NewObject<UHttpWebManager>(Instance); auto* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
		FRPGDungeonDepartureJournalTestAccess::Bind(Flow, Web, Directory);
		TestFalse(TEXT("A fresh manager never restores a credential"), Web->HasAccessToken());
		TestTrue(TEXT("Offline selection does not consume account storage"), Flow->SelectCharacter(Record.CharacterId));
		TestFalse(TEXT("No unauthenticated command is restored"), Flow->HasUnresolvedDeparture());
		if (!FRPGDungeonDepartureJournalTestAccess::Login(Web)) return false;
		if (!TestTrue(TEXT("Same account selection restores a persisted command"), Flow->SelectCharacter(Record.CharacterId))) return false;
		TestTrue(TEXT("Fresh flow keeps the original scope, ID, version and intent"), FRPGDungeonDepartureJournalTestAccess::Command(Flow).SameAs(Record));
		TestFalse(TEXT("A local hint is not a backend session"), Flow->HasActiveDungeonSession());
		TestFalse(TEXT("Restoration does not dispatch an automatic request"), Flow->IsRequestInFlight());
		TestFalse(TEXT("Pending command locks a new session"), Flow->CreateDungeonSession(TEXT("Dungeon.RaidSwordMan"), TEXT("Normal")));
		TestFalse(TEXT("Pending command locks joining another session"), Flow->JoinDungeonSession(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)));
		Flow->ResetDungeonFlow(false);
		TestTrue(TEXT("Local reset cannot discard the restored marker"), Flow->HasUnresolvedDeparture() && Flow->GetSelectedCharacterId() == Record.CharacterId);
		TestFalse(TEXT("Another character cannot replace unresolved scope"), Flow->SelectCharacter(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)));
		auto Unchanged = Snapshot(Record, bCancel);
		FRPGDungeonDepartureJournalTestAccess::Read(Flow, Unchanged);
		TestTrue(TEXT("An unchanged snapshot preserves both uncertain intents"), Flow->HasUnresolvedDeparture());
		auto Foreign = Snapshot(Record, true, TEXT("Loading")); Foreign.Members[0].CharacterId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		FRPGDungeonDepartureJournalTestAccess::Read(Flow, Foreign);
		TestTrue(TEXT("A nonmember read cannot remove persisted identity"), Flow->HasUnresolvedDeparture());
		FRPGDungeonDepartureJournalTestAccess::SetEndpoint(Web, TEXT("http://departure-fixture.invalid/other-api"));
		TestFalse(TEXT("A changed endpoint cannot replay or replace the restored command"), FRPGDungeonDepartureJournalTestAccess::Restore(Flow));
		FRPGDungeonDepartureCommand Loaded;
		TestTrue(TEXT("Scope failure preserves the original disk record"), Journal.Load(Loaded) == ERPGDepartureJournalLoad::Found);
		FRPGDungeonDepartureJournalTestAccess::Login(Web);
		FRPGDungeonDepartureJournalTestAccess::Read(Flow, Snapshot(Record, true, TEXT("Loading")));
		TestFalse(TEXT("Authenticated claim-winning snapshot resolves persisted uncertainty"), Flow->HasUnresolvedDeparture());
		TestTrue(TEXT("Resolution is durable for the next process"), Journal.Load(Loaded) == ERPGDepartureJournalLoad::Missing);
		if (!Journal.Persist(Record)) return false;
		Flow = NewObject<URPGDungeonSessionSubsystem>(Instance); FRPGDungeonDepartureJournalTestAccess::Bind(Flow, Web, Directory);
		if (!Flow->SelectCharacter(Record.CharacterId)) return false;
		FRPGDungeonDepartureJournalTestAccess::Read(Flow, FRPGDungeonSession());
		TestFalse(TEXT("Authenticated no-active-session also durably resolves a restored hint"), Flow->HasUnresolvedDeparture());
		TestTrue(TEXT("No-active-session cleanup reaches disk"), Journal.Load(Loaded) == ERPGDepartureJournalLoad::Missing);
		if (!Journal.Persist(Record)) return false;
		Flow = NewObject<URPGDungeonSessionSubsystem>(Instance); FRPGDungeonDepartureJournalTestAccess::Bind(Flow, Web, Directory);
		if (!Flow->SelectCharacter(Record.CharacterId)) return false;
		auto Confirmed = Snapshot(Record, !bCancel); if (bCancel) Confirmed.LobbyRevision = 12;
		FRPGDungeonDepartureJournalTestAccess::Read(Flow, Confirmed);
		TestFalse(TEXT("Matching committed Waiting snapshot resolves either original intent"), Flow->HasUnresolvedDeparture());
		TestTrue(TEXT("Committed Waiting resolution is also durable"), Journal.Load(Loaded) == ERPGDepartureJournalLoad::Missing);
	}
	return true;
}
#endif
