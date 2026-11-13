#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Manager/HttpWebManager.h"
#include "GameMode/RPGGameModeBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HttpModule.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Security/RPGSecurityTelemetrySubsystem.h"
#include "UObject/Package.h"

// Inject only server assignment and HTTP responses; never ProcessRequest,
// consume a real ticket, eject an actor, or bypass production gameplay authority.
struct FRPGDungeonMembershipTestAccess
{
	static void Configure(UHttpWebManager* Web, const FString& Session, const FString& Server)
	{
		Web->ConfiguredDungeonSessionId = Session;
		Web->GameServerId = Server;
		Web->BackendServiceToken = TEXT("membership-server-fixture");
	}
	static void RemoveToken(UHttpWebManager* Web) { Web->BackendServiceToken.Reset(); }
	static void SetToken(UHttpWebManager* Web, const FString& Token) { Web->BackendServiceToken = Token; }
	static FHttpRequestPtr Prepare(UHttpWebManager* Web, TFunction<void(FRPGDungeonMembershipSnapshotResult)> Callback)
	{ return Web->CreateConfiguredDungeonMembershipRequest(MoveTemp(Callback)); }
	static FHttpRequestPtr PrepareExpiration(UHttpWebManager* Web, TFunction<void()> Callback)
	{ return Web->CreateConfiguredDungeonDisconnectionExpirationsRequest(MoveTemp(Callback)); }
	static void Cancel(UHttpWebManager* Web) { Web->CancelDungeonMembershipRequest(); }
	static bool HasRequest(UHttpWebManager* Web) { return Web->DungeonMembershipTransport->IsActive(); }
	static FRPGDungeonMembershipMonitor& Monitor(ARPGGameModeBase* Mode) { return Mode->DungeonMembershipMonitor; }
	static bool AdmissionDenied(ARPGGameModeBase* Mode, const FString& Session, const FString& Character, const FString& Steam)
	{ return Mode->IsDungeonAdmissionAbandoned(Session, Character, Steam); }
	static bool Complete(ARPGGameModeBase* Mode, const FRPGDungeonMembershipSnapshotResult& Result)
	{
		uint64 Serial = 0;
		return Mode->DungeonMembershipMonitor.BeginRead(Serial) && Mode->CompleteDungeonMembershipRead(Serial, Result);
	}
	static void Age(ARPGGameModeBase* Mode) { Mode->DungeonMembershipPresentation.ReceivedAtSeconds -= 30.; }
	static void ConfigureTelemetry(URPGSecurityTelemetrySubsystem* Telemetry, const FString& Root)
	{
		Telemetry->bAvailable = true; Telemetry->bPersistToDisk = true;
		Telemetry->DungeonSessionId = TEXT("00000000-0000-0000-0000-000000000001");
		Telemetry->SpoolPath = FPaths::Combine(Root, TEXT("events.jsonl"));
		Telemetry->DeadLetterPath = FPaths::Combine(Root, TEXT("deadletter.jsonl"));
		Telemetry->RevocationPath = FPaths::Combine(Root, TEXT("revocations.json"));
	}
	static auto& Queue(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->Queue; }
	static bool Save(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->PersistQueueSnapshot(); }
	static bool Restore(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->RestorePersistedState(); }
	static bool PrepareDelivery(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->PrepareMembershipDelivery(); }
	static bool HasDelivery(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->ActiveRequest.IsValid(); }
	static void RevocationPath(URPGSecurityTelemetrySubsystem* Telemetry, const FString& Path) { Telemetry->RevocationPath = Path; }
	static void StopTelemetry(URPGSecurityTelemetrySubsystem* Telemetry) { Telemetry->bAvailable = false; }
};

namespace
{
	const FString Session = TEXT("00000000-0000-0000-0000-000000000001");
	const FString Character = TEXT("00000000-0000-0000-0000-000000000002");
	const FString Steam = TEXT("76561198000000001");
	const FString Server = TEXT("membership-server-fixture");
	const FString OtherSession = TEXT("00000000-0000-0000-0000-000000000003");
	const FString ExpelledCharacter = TEXT("00000000-0000-0000-0000-000000000005");
	const FString ExpelledSteam = TEXT("76561198000000003");
	FString SnapshotBody(bool bReceipt = true)
	{
		const FString Evidence = bReceipt ? FString::Printf(TEXT("{\"characterId\":\"%s\",\"steamId\":\"%s\",\"abandonedAt\":\"2026-10-10T00:00:00Z\",\"sessionStateAtAbandonment\":\"Closed\",\"rewardExcluded\":true}"), *Character, *Steam) : FString();
		return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"state\":\"Closed\",\"updatedAt\":\"2026-10-10T00:00:00Z\",\"expiresAt\":\"2026-10-10T01:00:00Z\",\"members\":[],\"abandonments\":[%s]}"), *Session, *Server, *Evidence);
	}
	FRPGDungeonMembershipSnapshotResult ReadResult(bool bReceipt = true)
	{
		FRPGDungeonMembershipSnapshotResult Result;
		Result.HttpStatus = 200;
		Result.bSuccess = FRPGDungeonMembershipSnapshotCodec::Decode(SnapshotBody(bReceipt), 200, Session, Server, Result.Snapshot);
		return Result;
	}
	FRPGDungeonMembershipSnapshotResult ExpulsionResult()
	{
		FRPGDungeonMembershipSnapshotResult Result;
		Result.HttpStatus = 200;
		const FString Receipt = FString::Printf(TEXT("{\"characterId\":\"%s\",\"steamId\":\"%s\",\"disconnectedAt\":\"2026-10-10T00:00:00Z\",\"expiredAt\":\"2026-10-10T00:10:00Z\",\"processedAt\":\"2026-10-10T00:10:01Z\",\"rewardExcluded\":true}"), *ExpelledCharacter, *ExpelledSteam);
		const FString Body = SnapshotBody(false).LeftChop(1)
			+ FString::Printf(TEXT(",\"disconnectionExpulsions\":[%s]}"), *Receipt);
		Result.bSuccess = FRPGDungeonMembershipSnapshotCodec::Decode(Body, 200, Session, Server, Result.Snapshot);
		return Result;
	}
	class FMembershipResponse final : public IHttpResponse
	{
	public:
		FMembershipResponse(int32 InCode, FString InBody) : Code(InCode), Body(MoveTemp(InBody))
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
		FString Body;
		FString Url = TEXT("http://membership-fixture.invalid/api");
		TArray<uint8> Bytes;
	};
	FHttpResponsePtr Response(int32 Code = 200, FString Body = SnapshotBody())
	{ return MakeShared<FMembershipResponse, ESPMode::ThreadSafe>(Code, MoveTemp(Body)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonMembershipMonitorTest, "ProjectRPG.Online.DungeonMembershipMonitor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonMembershipMonitorTest::RunTest(const FString&)
{
	FRPGDungeonMembershipMonitor Monitor;
	uint64 Serial = 0;
	TestFalse(TEXT("Unassigned monitor cannot poll"), Monitor.BeginRead(Serial));
	TestFalse(TEXT("Invalid session is rejected"), Monitor.Configure(TEXT("not-a-guid"), Server));
	TestFalse(TEXT("Missing server is rejected"), Monitor.Configure(Session, FString()));
	TestFalse(TEXT("Control/whitespace assignment is rejected"), Monitor.Configure(Session, TEXT("bad\nserver")));
	TestTrue(TEXT("Exact assignment configured"), Monitor.Configure(Session, Server));
	TestTrue(TEXT("Read begins"), Monitor.BeginRead(Serial));
	uint64 Duplicate = 0;
	TestFalse(TEXT("Only one read in-flight"), Monitor.BeginRead(Duplicate));
	TestTrue(TEXT("Empty valid view accepted"), Monitor.FinishRead(Serial, ReadResult(false)));
	TestFalse(TEXT("Missing member never denies admission"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	FRPGDungeonMembershipSnapshotResult FailedRead = ReadResult(); FailedRead.bSuccess = false;
	Monitor.BeginRead(Serial); Monitor.FinishRead(Serial, FailedRead);
	TestFalse(TEXT("Valid-looking body on failed read does not create evidence"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	TestTrue(TEXT("Next read begins"), Monitor.BeginRead(Duplicate));
	TestFalse(TEXT("Duplicate old completion cannot finish a new read"), Monitor.FinishRead(Serial, ReadResult()));
	TestTrue(TEXT("Explicit immutable receipt is recorded"), Monitor.FinishRead(Duplicate, ReadResult()));
	TestTrue(TEXT("Exact opt-out denies admission"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	TestFalse(TEXT("Other account cannot reuse receipt"), Monitor.IsExplicitlyAbandoned(Character, TEXT("76561198000000002")));
	TestFalse(TEXT("Remaining character is unaffected"), Monitor.IsExplicitlyAbandoned(OtherSession, Steam));
	for (const int32 Status : { 0, 401, 403, 404, 409, 429, 500 })
	{
		FRPGDungeonMembershipSnapshotResult Error = ReadResult(); Error.HttpStatus = Status; Error.bSuccess = false;
		Monitor.BeginRead(Serial);
		TestFalse(TEXT("HTTP/network errors are not new opt-out evidence"), Monitor.FinishRead(Serial, Error));
		TestTrue(TEXT("An error cannot restore already revoked admission"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	}
	Monitor.BeginRead(Serial);
	TestTrue(TEXT("Later empty view is harmless"), Monitor.FinishRead(Serial, ReadResult(false)));
	TestTrue(TEXT("Historical evidence remains monotonic"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	Monitor.BeginRead(Serial);
	TestTrue(TEXT("Valid independent expiry evidence accepted"), Monitor.FinishRead(Serial, ExpulsionResult()));
	TestTrue(TEXT("Exact expired character cached"), Monitor.IsDisconnectedExpelled(ExpelledCharacter, ExpelledSteam));
	TestFalse(TEXT("Expiry cannot be used as opt-out"), Monitor.IsExplicitlyAbandoned(ExpelledCharacter, ExpelledSteam));
	TestFalse(TEXT("Other account cannot reuse cached expiry"), Monitor.IsDisconnectedExpelled(ExpelledCharacter, Steam));
	Monitor.BeginRead(Serial);
	TestFalse(TEXT("Failed read cannot replace expiry evidence"), Monitor.FinishRead(Serial, FailedRead));
	TestTrue(TEXT("Failed read cannot restore expired membership"), Monitor.IsDisconnectedExpelled(ExpelledCharacter, ExpelledSteam));
	Monitor.BeginRead(Serial);
	TestTrue(TEXT("Later empty view is harmless to expiry"), Monitor.FinishRead(Serial, ReadResult(false)));
	TestTrue(TEXT("Expired evidence remains monotonic"), Monitor.IsDisconnectedExpelled(ExpelledCharacter, ExpelledSteam));
	FRPGDungeonMembershipSnapshotResult CrossReason = ExpulsionResult();
	CrossReason.Snapshot.DisconnectionExpulsions[0].CharacterId = Character;
	CrossReason.Snapshot.DisconnectionExpulsions[0].SteamId = Steam;
	Monitor.BeginRead(Serial);
	TestFalse(TEXT("One character cannot acquire conflicting revocation reasons"), Monitor.FinishRead(Serial, CrossReason));
	TestTrue(TEXT("Rejected read preserves existing expiry"), Monitor.IsDisconnectedExpelled(ExpelledCharacter, ExpelledSteam));
	FRPGDungeonMembershipSnapshotResult Inconsistent = ReadResult();
	auto NewEvidence = Inconsistent.Snapshot.Abandonments[0]; NewEvidence.Receipt.CharacterId = OtherSession;
	Inconsistent.Snapshot.Abandonments.Insert(NewEvidence, 0);
	Inconsistent.Snapshot.Abandonments.Last().Receipt.bRewardExcluded = false;
	Monitor.BeginRead(Serial);
	TestFalse(TEXT("Inconsistent native evidence is rejected atomically"), Monitor.FinishRead(Serial, Inconsistent));
	TestFalse(TEXT("Partial evidence cannot revoke another character"), Monitor.IsExplicitlyAbandoned(OtherSession, Steam));
	FRPGDungeonMembershipSnapshotResult Foreign = ReadResult(); Foreign.Snapshot.ServerId = TEXT("other-server");
	Monitor.BeginRead(Serial); TestFalse(TEXT("Foreign server read rejected"), Monitor.FinishRead(Serial, Foreign));
	Foreign = ReadResult(); Foreign.Snapshot.DungeonSessionId = OtherSession;
	Monitor.BeginRead(Serial); TestFalse(TEXT("Foreign session read rejected"), Monitor.FinishRead(Serial, Foreign));
	Monitor.BeginRead(Serial);
	TestTrue(TEXT("New assignment resets old evidence"), Monitor.Configure(OtherSession, Server));
	TestFalse(TEXT("Old scope completion ignored"), Monitor.FinishRead(Serial, ReadResult()));
	TestFalse(TEXT("New scope never inherits old opt-out"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	TestFalse(TEXT("New scope never inherits old expiry"), Monitor.IsDisconnectedExpelled(ExpelledCharacter, ExpelledSteam));
	Monitor.BeginRead(Duplicate);
	TestFalse(TEXT("Old scope cannot release new read"), Monitor.FinishRead(Serial, ReadResult()));
	Monitor.Reset(); TestFalse(TEXT("EndPlay-style reset drops outstanding callbacks"), Monitor.FinishRead(Duplicate, ReadResult()));

	// Exercise the same scope guard used before storing consumed admissions and
	// again before InitNewPlayer authenticates them. Not a real PreLogin/network test.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Admission fixture world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
	ARPGGameModeBase* Mode = World->SpawnActor<ARPGGameModeBase>();
	if (TestNotNull(TEXT("Production GameMode"), Mode))
	{
		auto& ModeMonitor = FRPGDungeonMembershipTestAccess::Monitor(Mode);
		ModeMonitor.Configure(Session, Server); ModeMonitor.BeginRead(Serial); ModeMonitor.FinishRead(Serial, ReadResult());
		TestTrue(TEXT("Production admission guard denies exact scope"), FRPGDungeonMembershipTestAccess::AdmissionDenied(Mode, Session, Character, Steam));
		TestFalse(TEXT("Admission for a new dungeon unaffected"), FRPGDungeonMembershipTestAccess::AdmissionDenied(Mode, OtherSession, Character, Steam));
		TestFalse(TEXT("Remaining party admission unaffected"), FRPGDungeonMembershipTestAccess::AdmissionDenied(Mode, Session, OtherSession, Steam));
		World->EndPlay(EEndPlayReason::Quit);
		TestFalse(TEXT("Production EndPlay invalidates admission evidence"), FRPGDungeonMembershipTestAccess::AdmissionDenied(Mode, Session, Character, Steam));
	}
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonMembershipTransportTest, "ProjectRPG.Online.DungeonMembershipTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonMembershipTransportTest::RunTest(const FString&)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	UHttpWebManager* Web = NewObject<UHttpWebManager>(Instance);
	FRPGDungeonMembershipTestAccess::Configure(Web, Session, Server);
	Web->SetAccessToken(TEXT("player-fixture-must-never-authorize-this-get"));
	TestFalse(TEXT("Normal client runtime cannot submit server GET"), Web->LoadConfiguredDungeonMembershipSnapshot([](auto) {}));
	TestFalse(TEXT("Normal client runtime cannot submit expiry POST"), Web->ProcessConfiguredDungeonDisconnectionExpirations([]() {}));
	int32 Calls = 0;
	FRPGDungeonMembershipSnapshotResult Last;
	auto Complete = [&](FRPGDungeonMembershipSnapshotResult Result) { ++Calls; Last = MoveTemp(Result); };
	FHttpRequestPtr Request = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	if (!TestTrue(TEXT("Fixture builds production request without dispatch"), Request.IsValid())) return false;
	TestEqual(TEXT("Exact scoped route"), Request->GetURL(), FString(TEXT("http://localhost:3000/api/dungeon-sessions/")) + Session + TEXT("/membership-snapshot"));
	TestEqual(TEXT("Read-only verb"), Request->GetVerb(), FString(TEXT("GET")));
	TestEqual(TEXT("Only server bearer, never player fallback"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer membership-server-fixture")));
	TestEqual(TEXT("Bodyless GET"), Request->GetContent().Num(), 0);
	TestEqual(TEXT("Actor-facing completion runs on the game thread"), Request->GetDelegateThreadPolicy(),
		EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	TestFalse(TEXT("Second request cannot replace first callback"), FRPGDungeonMembershipTestAccess::Prepare(Web, Complete).IsValid());
	Request->OnProcessRequestComplete().Execute(Request, Response(), true);
	TestTrue(TEXT("Production decoder returns scoped receipt"), Calls == 1 && Last.bSuccess && Last.Snapshot.IsExplicitlyAbandoned(Session, Server, Character, Steam));
	TestFalse(TEXT("Completion releases slot"), FRPGDungeonMembershipTestAccess::HasRequest(Web));
	Request->OnProcessRequestComplete().Execute(Request, Response(), true);
	TestEqual(TEXT("Duplicate callback ignored"), Calls, 1);
	FHttpRequestPtr Next = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	Request->OnProcessRequestComplete().Execute(Request, Response(401), true);
	TestTrue(TEXT("Old 401 cannot release the new read"), FRPGDungeonMembershipTestAccess::HasRequest(Web));
	TestEqual(TEXT("Old 401 cannot call old consumer again"), Calls, 1);
	TestTrue(TEXT("Server reads never invalidate player credentials"), Web->HasAccessToken());
	Next->OnProcessRequestComplete().Execute(Next, Response(401), true);
	TestTrue(TEXT("Current 401 yields failure with no evidence"), Calls == 2 && !Last.bSuccess && Last.Snapshot.Abandonments.IsEmpty());
	for (const int32 Status : { 403, 404, 409, 429, 500 })
	{
		Next = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
		Next->OnProcessRequestComplete().Execute(Next, Response(Status), true);
		TestTrue(TEXT("Rejected HTTP has no opt-out evidence"), !Last.bSuccess && Last.Snapshot.Abandonments.IsEmpty());
	}
	for (const FString Body : { FString(TEXT("{}")), SnapshotBody().Replace(*Session, *OtherSession),
		SnapshotBody().Replace(*Server, TEXT("other-server")), FString::ChrN(64 * 1024 + 1, TEXT(' ')) })
	{
		Next = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
		Next->OnProcessRequestComplete().Execute(Next, Response(200, Body), true);
		TestTrue(TEXT("Malformed/scoped/oversized response never revokes"), !Last.bSuccess && Last.Snapshot.Abandonments.IsEmpty());
	}
	Next = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	Next->OnProcessRequestComplete().Execute(Next, Response(), false);
	TestFalse(TEXT("Transport failure ignores even a valid-looking body"), Last.bSuccess);
	Next = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	FRPGDungeonMembershipTestAccess::Configure(Web, OtherSession, Server);
	Next->OnProcessRequestComplete().Execute(Next, Response(), true);
	TestFalse(TEXT("Changed assignment ignores old evidence"), Last.bSuccess);
	FRPGDungeonMembershipTestAccess::Configure(Web, Session, Server);
	Next = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	auto LateCallback = Next->OnProcessRequestComplete();
	const int32 CallsBeforeCancel = Calls;
	FRPGDungeonMembershipTestAccess::Cancel(Web);
	LateCallback.Execute(Next, Response(), true);
	TestEqual(TEXT("Cancelled generation cannot notify"), Calls, CallsBeforeCancel);
	TestFalse(TEXT("Cancel frees slot"), FRPGDungeonMembershipTestAccess::HasRequest(Web));
	FRPGDungeonMembershipTestAccess::RemoveToken(Web);
	TestFalse(TEXT("Missing server bearer never falls back to player token"), FRPGDungeonMembershipTestAccess::Prepare(Web, Complete).IsValid());
	FRPGDungeonMembershipTestAccess::SetToken(Web, TEXT("unsafe\r\nheader"));
	TestFalse(TEXT("Server bearer cannot inject headers"), FRPGDungeonMembershipTestAccess::Prepare(Web, Complete).IsValid());
	FRPGDungeonMembershipTestAccess::Configure(Web, Session, Server);
	FHttpRequestPtr Reentrant;
	Next = FRPGDungeonMembershipTestAccess::Prepare(Web, [&](FRPGDungeonMembershipSnapshotResult Result)
	{
		Complete(MoveTemp(Result));
		Reentrant = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	});
	Next->OnProcessRequestComplete().Execute(Next, Response(), true);
	TestTrue(TEXT("Completion may safely start the next read"), Reentrant.IsValid());
	if (Reentrant.IsValid()) Reentrant->OnProcessRequestComplete().Execute(Reentrant, Response(), true);
	int32 ExpirationCallbacks = 0;
	FHttpRequestPtr Expiration = FRPGDungeonMembershipTestAccess::PrepareExpiration(Web,
		[&ExpirationCallbacks]() { ++ExpirationCallbacks; });
	if (!TestTrue(TEXT("Server-only expiry request can be prepared without dispatch"), Expiration.IsValid())) return false;
	TestEqual(TEXT("Expiry route is exact assigned session"), Expiration->GetURL(),
		FString(TEXT("http://localhost:3000/api/dungeon-sessions/")) + Session + TEXT("/disconnection-expirations"));
	TestEqual(TEXT("Expiry uses POST"), Expiration->GetVerb(), FString(TEXT("POST")));
	TestEqual(TEXT("Expiry POST has no client timestamp or character body"), Expiration->GetContent().Num(), 0);
	TestEqual(TEXT("Expiry uses only server bearer"), Expiration->GetHeader(TEXT("Authorization")),
		FString(TEXT("Bearer membership-server-fixture")));
	TestEqual(TEXT("Expiry callback is game-thread scoped"), Expiration->GetDelegateThreadPolicy(),
		EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	TestFalse(TEXT("Expiry and membership GET share one in-flight slot"),
		FRPGDungeonMembershipTestAccess::Prepare(Web, Complete).IsValid());
	const int32 ReadsBeforeExpiry = Calls;
	Expiration->OnProcessRequestComplete().Execute(Expiration, Response(403), true);
	TestEqual(TEXT("Closed-session POST denial still completes follow-up"), ExpirationCallbacks, 1);
	TestEqual(TEXT("POST is not a membership read or evidence"), Calls, ReadsBeforeExpiry);
	TestFalse(TEXT("POST completion frees slot for grace-window GET"), FRPGDungeonMembershipTestAccess::HasRequest(Web));
	FHttpRequestPtr ReadAfterExpiration;
	Expiration = FRPGDungeonMembershipTestAccess::PrepareExpiration(Web, [&]()
	{
		++ExpirationCallbacks;
		ReadAfterExpiration = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	});
	Expiration->OnProcessRequestComplete().Execute(Expiration, Response(200, TEXT("{}")), true);
	TestTrue(TEXT("POST body is ignored and validated GET may start reentrantly"), ReadAfterExpiration.IsValid());
	if (ReadAfterExpiration.IsValid())
		ReadAfterExpiration->OnProcessRequestComplete().Execute(ReadAfterExpiration, Response(), true);
	TestEqual(TEXT("POST never invents a snapshot callback"), ExpirationCallbacks, 2);
	FHttpRequestPtr StaleExpiration = FRPGDungeonMembershipTestAccess::PrepareExpiration(Web,
		[&ExpirationCallbacks]() { ++ExpirationCallbacks; });
	if (!TestTrue(TEXT("Stale expiry fixture prepared"), StaleExpiration.IsValid())) return false;
	auto LateExpirationCallback = StaleExpiration->OnProcessRequestComplete();
	FRPGDungeonMembershipTestAccess::Cancel(Web);
	LateExpirationCallback.Execute(StaleExpiration, Response(), true);
	TestEqual(TEXT("Cancelled expiry cannot resume a read"), ExpirationCallbacks, 2);
	TestFalse(TEXT("Cancelled expiry frees the shared slot"), FRPGDungeonMembershipTestAccess::HasRequest(Web));
	StaleExpiration = FRPGDungeonMembershipTestAccess::PrepareExpiration(Web,
		[&ExpirationCallbacks]() { ++ExpirationCallbacks; });
	if (!TestTrue(TEXT("Scope-change expiry fixture prepared"), StaleExpiration.IsValid())) return false;
	FRPGDungeonMembershipTestAccess::Configure(Web, OtherSession, Server);
	StaleExpiration->OnProcessRequestComplete().Execute(StaleExpiration, Response(), true);
	TestEqual(TEXT("Changed assignment ignores old expiry completion"), ExpirationCallbacks, 2);
	FRPGDungeonMembershipTestAccess::Configure(Web, Session, Server);
	int32 FailedExpirationFollowups = 0;
	FHttpRequestPtr FailedExpiration = FRPGDungeonMembershipTestAccess::PrepareExpiration(Web,
		[&FailedExpirationFollowups]() { ++FailedExpirationFollowups; });
	if (!TestTrue(TEXT("Failed expiry fixture prepared"), FailedExpiration.IsValid())) return false;
	FailedExpiration->OnProcessRequestComplete().Execute(FailedExpiration, Response(500), false);
	TestEqual(TEXT("Transport failure still permits GET of previous receipts"), FailedExpirationFollowups, 1);
	FRPGDungeonMembershipTestAccess::RemoveToken(Web);
	TestFalse(TEXT("Expiry POST cannot fall back to player bearer"),
		FRPGDungeonMembershipTestAccess::PrepareExpiration(Web, []() {}).IsValid());
	FRPGDungeonMembershipTestAccess::SetToken(Web, TEXT("unsafe\r\nheader"));
	TestFalse(TEXT("Expiry server bearer cannot inject headers"),
		FRPGDungeonMembershipTestAccess::PrepareExpiration(Web, []() {}).IsValid());
	FRPGDungeonMembershipTestAccess::Configure(Web, Session, Server);
	const int32 CallsBeforeShutdown = Calls;
	FRPGDungeonMembershipTestAccess::Configure(Web, Session, Server);
	Next = FRPGDungeonMembershipTestAccess::Prepare(Web, Complete);
	LateCallback = Next->OnProcessRequestComplete();
	Web->Deinitialize();
	LateCallback.Execute(Next, Response(), true);
	TestEqual(TEXT("Deinitialize invalidates callback despite live UObject"), Calls, CallsBeforeShutdown);
	FRPGDungeonMembershipTestAccess::Configure(Web, Session, Server);
	TestFalse(TEXT("Deinitialized manager cannot restart reads"), FRPGDungeonMembershipTestAccess::Prepare(Web, Complete).IsValid());
	return true;
}

namespace RPGTelemetryMembershipTests
{
	using Access = FRPGDungeonMembershipTestAccess;
	FRPGSecurityTelemetryEvent Event(const FString& Id, const FString& Account)
	{
		FRPGSecurityTelemetryEvent Result;
		Result.EventId = FGuid::NewGuid(); Result.CharacterId = Id; Result.SteamId = Account;
		Result.Type = ERPGSecurityViolationType::InvalidCombatHit;
		Result.Severity = ERPGSecurityViolationSeverity::High;
		Result.Score = 1; Result.RiskAfter = 2; Result.ServerTimeSeconds = 3;
		return Result;
	}
	struct FFixture
	{
		FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests/TelemetryMembershipBarrier"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits));
		UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
		UWorld* World = nullptr;
		ARPGGameModeBase* Mode = nullptr;
		UHttpWebManager* Web = nullptr;
		URPGSecurityTelemetrySubsystem* Telemetry = nullptr;
		FFixture()
		{
			Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/TelemetryMembership_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			World = Instance->GetWorld();
			if (!World) return;
			FURL Url; Url.AddOption(TEXT("game=/Script/Project_RPG.RPGGameModeBase"));
			if (!World->SetGameMode(Url)) return;
			Mode = World->GetAuthGameMode<ARPGGameModeBase>();
			Web = Instance->GetSubsystem<UHttpWebManager>();
			if (!Mode || !Web || !Access::Monitor(Mode).Configure(Session, Server)) return;
			Access::Configure(Web, Session, Server);
			IFileManager::Get().MakeDirectory(*Root, true);
			Telemetry = NewObject<URPGSecurityTelemetrySubsystem>(Instance);
			Access::ConfigureTelemetry(Telemetry, Root);
		}
		~FFixture()
		{
			if (Telemetry)
			{
				Access::StopTelemetry(Telemetry);
				if (World) World->GetTimerManager().ClearAllTimersForObject(Telemetry);
			}
			Instance->Shutdown();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		}
		FString Path(const TCHAR* Name) const { return FPaths::Combine(Root, Name); }
		FString Read(const TCHAR* Name) const
		{ FString Text; FFileHelper::LoadFileToString(Text, *Path(Name)); return Text; }
		void AddLegacyEvents()
		{
			Access::Queue(Telemetry).Enqueue(Event(Character, Steam));
			Access::Queue(Telemetry).Enqueue(Event(OtherSession, TEXT("76561198000000002")));
			Access::Save(Telemetry);
		}
	}; // Synthetic native HTTP results + isolated files, not a real restart/HTTP test.
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryInitialMembershipBarrierTest,
	"ProjectRPG.Security.TelemetryMembership.InitialReadBarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryInitialMembershipBarrierTest::RunTest(const FString&)
{
	using namespace RPGTelemetryMembershipTests;
	FFixture F;
	if (!TestNotNull(TEXT("Telemetry membership fixture"), F.Telemetry)) return false;
	F.AddLegacyEvents();
	const FString Original = F.Read(TEXT("events.jsonl"));
	TestFalse(TEXT("Legacy spool has no local tombstone"), IFileManager::Get().FileExists(*F.Path(TEXT("revocations.json"))));
	TestFalse(TEXT("FlushNow cannot deliver before the first validated read"), F.Telemetry->FlushNow());
	TestFalse(TEXT("The initial barrier creates no HTTP request"), Access::HasDelivery(F.Telemetry));
	TestFalse(TEXT("The initial barrier reserves no mixed batch"), Access::Queue(F.Telemetry).HasBatchInFlight());
	TestEqual(TEXT("The entire legacy spool remains byte-for-byte"), F.Read(TEXT("events.jsonl")), Original);
	auto Failed = ReadResult(); Failed.bSuccess = false; Failed.HttpStatus = 503;
	TestFalse(TEXT("A failed first read cannot open delivery"), Access::Complete(F.Mode, Failed));
	TestFalse(TEXT("Valid-looking failed evidence cannot quarantine or submit"), Access::PrepareDelivery(F.Telemetry));
	TestEqual(TEXT("Failed read preserves all queued events"), F.Telemetry->GetQueuedEventCount(), 2);
	TestTrue(TEXT("A matching validated empty read is accepted"), Access::Complete(F.Mode, ReadResult(false)));
	TestTrue(TEXT("An empty membership view opens transport without inventing revocations"), Access::PrepareDelivery(F.Telemetry));
	TestEqual(TEXT("Missing members do not remove events"), F.Telemetry->GetQueuedEventCount(), 2);
	TestEqual(TEXT("Read-only gate does not rewrite a valid spool"), F.Read(TEXT("events.jsonl")), Original);
	Access::Age(F.Mode);
	TestFalse(TEXT("A stale presentation defers delivery, not eligibility"), Access::PrepareDelivery(F.Telemetry));
	TestEqual(TEXT("Stale read preserves the original evidence"), F.Read(TEXT("events.jsonl")), Original);
	TestTrue(TEXT("A later successful read resumes delivery"), Access::Complete(F.Mode, ReadResult(false)));
	Access::Configure(F.Web, Session, TEXT("replacement-server"));
	TestFalse(TEXT("An old server view cannot release a replacement assignment"), Access::PrepareDelivery(F.Telemetry));
	Access::Configure(F.Web, OtherSession, Server);
	TestFalse(TEXT("An old session view cannot release a new session"), Access::PrepareDelivery(F.Telemetry));
	Access::Configure(F.Web, Session, Server); Access::RemoveToken(F.Web);
	TestFalse(TEXT("Missing configured server authority cannot open delivery"), Access::PrepareDelivery(F.Telemetry));
	Access::Configure(F.Web, Session, Server);
	TestTrue(TEXT("Restoring the exact authority/read restores only delivery eligibility"), Access::PrepareDelivery(F.Telemetry));
	TestFalse(TEXT("The gate never sends a GET/POST itself"), Access::HasDelivery(F.Telemetry));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTelemetryReceiptBeforeDeliveryTest,
	"ProjectRPG.Security.TelemetryMembership.ReceiptBeforeDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTelemetryReceiptBeforeDeliveryTest::RunTest(const FString&)
{
	using namespace RPGTelemetryMembershipTests;
	FFixture F;
	if (!TestNotNull(TEXT("Telemetry receipt fixture"), F.Telemetry)) return false;
	F.AddLegacyEvents();
	const auto ForeignAccount = Event(Character, TEXT("76561198000000002"));
	const auto Expired = Event(ExpelledCharacter, ExpelledSteam);
	Access::Queue(F.Telemetry).Enqueue(ForeignAccount); Access::Queue(F.Telemetry).Enqueue(Expired);
	TestTrue(TEXT("Mixed legacy spool saved"), Access::Save(F.Telemetry));
	const auto Original = Access::Queue(F.Telemetry).GetEvents();
	Access::StopTelemetry(F.Telemetry);
	F.Telemetry = NewObject<URPGSecurityTelemetrySubsystem>(F.Instance);
	Access::ConfigureTelemetry(F.Telemetry, F.Root);
	TestTrue(TEXT("A new subsystem can restore a spool without any local tombstones"), Access::Restore(F.Telemetry));
	auto Receipts = ReadResult(); Receipts.Snapshot.DisconnectionExpulsions = ExpulsionResult().Snapshot.DisconnectionExpulsions;
	TestTrue(TEXT("Authority accepts immutable abandonment and expiry receipts"), Access::Complete(F.Mode, Receipts));
	TestEqual(TEXT("No authenticated actor is needed for stale spool quarantine"), F.World->GetNumPlayerControllers(), 0);
	TestTrue(TEXT("Receipt quarantine completes before any delivery reservation"), Access::PrepareDelivery(F.Telemetry));
	const auto Remaining = Access::Queue(F.Telemetry).GetEvents();
	if (TestEqual(TEXT("Only the exact two revoked identities are removed from pending delivery"), Remaining.Num(), 2))
	{
		TestEqual(TEXT("Remaining member EventId and queue order are retained"), Remaining[0].EventId, Original[1].EventId);
		TestEqual(TEXT("Same character with a different account is not falsely revoked"), Remaining[1].EventId, ForeignAccount.EventId);
	}
	TestTrue(TEXT("Revocation evidence is durable before delivery"), IFileManager::Get().FileExists(*F.Path(TEXT("revocations.json"))));
	const FString DeadLetter = F.Read(TEXT("deadletter.jsonl"));
	TestTrue(TEXT("Abandoned event is preserved, not erased"), DeadLetter.Contains(Original[0].EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	TestTrue(TEXT("Expired event is preserved, not erased"), DeadLetter.Contains(Expired.EventId.ToString(EGuidFormats::DigitsWithHyphens)));
	TestTrue(TEXT("Repeated gating is harmless"), Access::PrepareDelivery(F.Telemetry));
	TestEqual(TEXT("Already quarantined events are not appended again"), F.Read(TEXT("deadletter.jsonl")), DeadLetter);
	TestTrue(TEXT("Later empty presentation does not erase immutable monitor evidence"), Access::Complete(F.Mode, ReadResult(false)));
	Access::Queue(F.Telemetry).Enqueue(Event(Character, Steam));
	TestTrue(TEXT("A later pending event is still filtered from the monitor"), Access::PrepareDelivery(F.Telemetry));
	TestEqual(TEXT("Only the same remaining identities stay queued"), F.Telemetry->GetQueuedEventCount(), 2);
	TestFalse(TEXT("Quarantine does not submit HTTP or reserve a batch"), Access::HasDelivery(F.Telemetry) || Access::Queue(F.Telemetry).HasBatchInFlight());

	FFixture Blocked;
	if (!TestNotNull(TEXT("Durability failure fixture"), Blocked.Telemetry)) return false;
	Blocked.AddLegacyEvents();
	const FString Before = Blocked.Read(TEXT("events.jsonl"));
	TestTrue(TEXT("Failure fixture gets the real receipt shape"), Access::Complete(Blocked.Mode, ReadResult()));
	TestTrue(TEXT("A file blocks the intended journal parent"), FFileHelper::SaveStringToFile(TEXT("blocker"), *Blocked.Path(TEXT("blocked"))));
	Access::RevocationPath(Blocked.Telemetry, FPaths::Combine(Blocked.Path(TEXT("blocked")), TEXT("revocations.json")));
	AddExpectedError(TEXT("Revoked member audit quarantine could not be persisted"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Failed durable quarantine keeps delivery closed"), Access::PrepareDelivery(Blocked.Telemetry));
	TestEqual(TEXT("Durability failure preserves the original queue/spool"), Blocked.Read(TEXT("events.jsonl")), Before);
	TestEqual(TEXT("Durability failure retains all pending events"), Blocked.Telemetry->GetQueuedEventCount(), 2);
	TestFalse(TEXT("Preservation failure does not reserve or send"), Access::HasDelivery(Blocked.Telemetry) || Access::Queue(Blocked.Telemetry).HasBatchInFlight());
	Access::RevocationPath(Blocked.Telemetry, Blocked.Path(TEXT("revocations.json")));
	TestTrue(TEXT("Repair retries the same pending evidence without a new EventId"), Access::PrepareDelivery(Blocked.Telemetry));
	TestEqual(TEXT("The retained peer remains after successful repair"), Blocked.Telemetry->GetQueuedEventCount(), 1);
	return true;
}
#endif
