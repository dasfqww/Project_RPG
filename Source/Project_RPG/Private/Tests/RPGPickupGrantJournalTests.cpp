#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Backend/RPGItemBackendSubsystem.h"
#include "Item/Transaction/RPGPickupGrantJournal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

struct FRPGPickupJournalSubsystemTestAccess
{
	static void Configure(URPGItemBackendSubsystem& Subsystem,
		TSharedRef<FRPGPickupGrantJournal> Journal,
		TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> Transport,
		bool bRestored)
	{
		Subsystem.PickupJournal = Journal;
		Subsystem.Gateway = MakeShared<FRPGItemBackendGateway>(Transport, 1);
		Subsystem.RestoredPickupIds.Reset();
		if (bRestored)
			for (const auto& Entry : Journal->GetPending()) Subsystem.RestoredPickupIds.Add(Entry.Key);
	}
};

namespace RPGPickupJournalTests
{
FRPGItemRepositoryCommitRequest MakeRequest()
{
	FRPGItemRepositoryCommitRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Operation = TEXT("Item.Pickup");
	Request.CommandFingerprint = TEXT("pickup-journal-fixture");
	Request.Actor.Type = ERPGItemOwnerType::Character;
	Request.Actor.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Request.AffectedQuantity = 3;
	FRPGItemInstanceState State;
	FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 104729, 3, {}, {}, State);
	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Inventory;
	Location.ContainerId = Request.Actor.OwnerId;
	Location.SlotIndex = 0;
	FRPGItemRecordMetadata Metadata;
	Metadata.CreationSource = TEXT("WorldPickup");
	FRPGItemRecord Record;
	FRPGItemRecord::TryCreate(FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("JournalPickup")),
		1, Request.Actor, Location, State, Metadata, Record);
	Request.Mutations.AddDefaulted_GetRef().NewRecord = Record;
	return Request;
}

FRPGItemBackendCommitResult MakeReceipt(const FRPGItemRepositoryCommitRequest& Request)
{
	FRPGItemBackendCommitResult Receipt;
	Receipt.Status = ERPGItemBackendStatus::Succeeded;
	Receipt.RequestId = Request.RequestId;
	Receipt.Operation = Request.Operation;
	Receipt.CommandFingerprint = Request.CommandFingerprint;
	Receipt.Actor = Request.Actor;
	Receipt.AffectedQuantity = Request.AffectedQuantity;
	Receipt.Records.Add(Request.Mutations[0].NewRecord.CopyWithRevision(1));
	return Receipt;
}

FString Serialize(const TSharedRef<FJsonObject>& Object)
{
	FString Json;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	return Json;
}

FRPGItemBackendHttpResponse ReceiptResponse(const FRPGItemRepositoryCommitRequest& Request)
{
	FString Body;
	FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Body);
	TSharedPtr<FJsonObject> Root;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root);
	const auto Record = Root->GetArrayField(TEXT("mutations"))[0]->AsObject()->GetObjectField(TEXT("newRecord"));
	Record->SetNumberField(TEXT("revision"), 1);
	Root->RemoveField(TEXT("mutations"));
	Root->SetStringField(TEXT("status"), TEXT("Committed"));
	Root->SetArrayField(TEXT("records"), { MakeShared<FJsonValueObject>(Record) });
	FRPGItemBackendHttpResponse Response;
	Response.bTransportSuccessful = true;
	Response.StatusCode = 200;
	Response.Body = Serialize(Root.ToSharedRef());
	return Response;
}

class FTransport final : public IRPGItemBackendTransport
{
public:
	TArray<FRPGItemBackendHttpRequest> Requests;
	FRPGItemBackendHttpCompletion Pending;
	TFunction<void()> OnSend;
	virtual void Send(const FRPGItemBackendHttpRequest& Request, FRPGItemBackendHttpCompletion Completion) override
	{
		Requests.Add(Request);
		Pending = MoveTemp(Completion);
		if (OnSend) OnSend();
	}
	void Reply(FRPGItemBackendHttpResponse Response)
	{
		auto Callback = MoveTemp(Pending);
		if (Callback) Callback(MoveTemp(Response));
	}
	void ReplyStatus(int32 Status, FString Body = TEXT(""))
	{
		FRPGItemBackendHttpResponse Response;
		Response.bTransportSuccessful = true;
		Response.StatusCode = Status;
		Response.Body = MoveTemp(Body);
		Reply(MoveTemp(Response));
	}
};

struct FFixture
{
	FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests/ItemPickupJournal"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FString Server = TEXT("journal-test-server");
	FString Session = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	FGuid Drop = FGuid::NewGuid();
	FRPGItemRepositoryCommitRequest Request = MakeRequest();
	TSharedRef<FRPGPickupGrantJournal> Journal = MakeJournal();
	TSharedRef<FTransport, ESPMode::ThreadSafe> Transport = MakeShared<FTransport, ESPMode::ThreadSafe>();
	TStrongObjectPtr<UGameInstance> Instance{ NewObject<UGameInstance>() };
	TStrongObjectPtr<URPGItemBackendSubsystem> Subsystem{ NewObject<URPGItemBackendSubsystem>(Instance.Get()) };
	TSharedRef<FRPGPickupGrantJournal> MakeJournal() const
	{
		return MakeShared<FRPGPickupGrantJournal>(Directory, Server, Session);
	}
	FString File() const
	{
		return FPaths::Combine(Directory, Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT(".json"));
	}
	bool Configure(bool bRestored)
	{
		if (!Journal->Restore()) return false;
		FRPGPickupJournalSubsystemTestAccess::Configure(*Subsystem, Journal, Transport, bRestored);
		return true;
	}
};

FRPGItemBackendHttpResponse OriginResponse(const FFixture& F, const bool bConsumed = false)
{
	FString Body; FRPGItemBackendJsonCodec::SerializeCommitRequest(F.Request, Body);
	TSharedPtr<FJsonObject> Command; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Command);
	const auto Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("Prepared")); Root->SetStringField(TEXT("serverId"), F.Server);
	Root->SetStringField(TEXT("dungeonSessionId"), F.Session);
	Root->SetStringField(TEXT("dropId"), F.Drop.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("requestId"), F.Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("characterId"), F.Request.Actor.OwnerId);
	Root->SetStringField(TEXT("itemId"), F.Request.Mutations[0].NewRecord.GetItemId().ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("preparedAt"), TEXT("2026-10-14T00:00:00Z"));
	if (bConsumed) Root->SetStringField(TEXT("consumedAt"), TEXT("2026-10-14T00:00:01Z"));
	else Root->SetField(TEXT("consumedAt"), MakeShared<FJsonValueNull>());
	Root->SetObjectField(TEXT("command"), Command);
	FRPGItemBackendHttpResponse Response;
	Response.bTransportSuccessful = true; Response.StatusCode = 200; Response.Body = Serialize(Root);
	return Response;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupJournalFreezeTest,
	"ProjectRPG.Item.PickupJournal.DurableFrozenCommandAndFullReceipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupJournalFreezeTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!TestTrue(TEXT("Empty journal restores"), F.Journal->Restore())) return false;
	TestTrue(TEXT("Frozen command persisted"), F.Journal->Persist(F.Request));
	TestTrue(TEXT("Identical command is idempotent"), F.Journal->Persist(F.Request));
	auto Changed = F.Request;
	Changed.CommandFingerprint += TEXT("-changed");
	TestFalse(TEXT("Same request cannot change its body"), F.Journal->Persist(Changed));
	Changed = F.Request;
	Changed.Mutations[0].NewRecord = MakeRequest().Mutations[0].NewRecord;
	TestFalse(TEXT("Same fingerprint cannot conceal a different item/owner"), F.Journal->Persist(Changed));
	const auto Restarted = F.MakeJournal();
	if (!TestTrue(TEXT("Fresh journal restores from disk"), Restarted->Restore())) return false;
	if (!TestEqual(TEXT("Exactly one pending request"), Restarted->GetPending().Num(), 1)) return false;
	FString OriginalBody, RestoredBody;
	FRPGItemBackendJsonCodec::SerializeCommitRequest(F.Request, OriginalBody);
	FRPGItemBackendJsonCodec::SerializeCommitRequest(Restarted->GetPending().FindChecked(F.Request.RequestId), RestoredBody);
	TestEqual(TEXT("Every frozen field survives restart"), RestoredBody, OriginalBody);
	auto Receipt = MakeReceipt(F.Request);
	Receipt.Records[0] = Receipt.Records[0].CopyWithRevision(2);
	TestFalse(TEXT("Different full record cannot resolve"), Restarted->Resolve(Receipt));
	TestTrue(TEXT("Malformed success preserves durable entry"), IFileManager::Get().FileExists(*F.File()));
	TestTrue(TEXT("Matching DB receipt resolves"), Restarted->Resolve(MakeReceipt(F.Request)));
	TestFalse(TEXT("Only verified pending entry is removed"), IFileManager::Get().FileExists(*F.File()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupJournalTemporaryTest,
	"ProjectRPG.Item.PickupJournal.RestoresTemporaryFileAndRejectsConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupJournalTemporaryTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!F.Journal->Restore() || !F.Journal->Persist(F.Request)) return false;
	TestTrue(TEXT("Simulate interrupted rename using only fixture file"),
		IFileManager::Get().Move(*(F.File() + TEXT(".tmp")), *F.File(), false));
	const auto Restarted = F.MakeJournal();
	TestTrue(TEXT("Valid temporary file restores"), Restarted->Restore());
	TestTrue(TEXT("Temporary entry promoted"), IFileManager::Get().FileExists(*F.File()));
	FString Contents;
	FFileHelper::LoadFileToString(Contents, *F.File());
	Contents.ReplaceInline(TEXT("pickup-journal-fixture"), TEXT("pickup-journal-conflict"));
	FFileHelper::SaveStringToFile(Contents, *(F.File() + TEXT(".tmp")));
	const auto Conflicted = F.MakeJournal();
	TestFalse(TEXT("Different main/tmp bodies fail closed"), Conflicted->Restore());
	TestTrue(TEXT("Main evidence retained"), IFileManager::Get().FileExists(*F.File()));
	TestTrue(TEXT("Temporary evidence retained"), IFileManager::Get().FileExists(*(F.File() + TEXT(".tmp"))));
	TestEqual(TEXT("No partial in-memory restore"), Conflicted->GetPending().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupJournalScopeTest,
	"ProjectRPG.Item.PickupJournal.RejectsScopeCorruptionAndOversizedFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupJournalScopeTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!F.Journal->Restore() || !F.Journal->Persist(F.Request)) return false;
	FRPGPickupGrantJournal WrongServer(F.Directory, TEXT("another-server"), F.Session);
	TestFalse(TEXT("Allocation server mismatch rejected"), WrongServer.Restore());
	FRPGPickupGrantJournal WrongSession(F.Directory, F.Server, FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	TestFalse(TEXT("Dungeon session mismatch rejected"), WrongSession.Restore());
	FFileHelper::SaveStringToFile(TEXT("{"), *(F.File() + TEXT(".tmp")));
	const auto Corrupt = F.MakeJournal();
	TestFalse(TEXT("Truncated tmp does not permit partial restore"), Corrupt->Restore());
	TestTrue(TEXT("Original file remains"), IFileManager::Get().FileExists(*F.File()));
	FFixture Large;
	IFileManager::Get().MakeDirectory(*Large.Directory, true);
	FFileHelper::SaveStringToFile(FString::ChrN(70 * 1024, TEXT('x')), *Large.File());
	TestFalse(TEXT("Bounded file read rejects oversize"), Large.Journal->Restore());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupJournalSubsystemRecoveryTest,
	"ProjectRPG.Item.PickupJournal.SubsystemLostAckReadOnlyRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupJournalSubsystemRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!TestTrue(TEXT("Live subsystem configured"), F.Configure(false))) return false;
	F.Transport->OnSend = [&]()
	{
		const auto OnDisk = F.MakeJournal();
		TestTrue(TEXT("Disk record exists BEFORE first HTTP send"), OnDisk->Restore());
		TestEqual(TEXT("Exactly one durable pending command before POST"), OnDisk->GetPending().Num(), 1);
		TestEqual(TEXT("Drop identity is durable before prepare"), OnDisk->GetDropId(F.Request.RequestId), F.Drop);
	};
	bool bCommitCompleted = false;
	TestTrue(TEXT("Prepared pickup submitted"), F.Subsystem->CommitPickup(F.Drop, F.Request,
		[&](FRPGItemBackendCommitResult R) { bCommitCompleted = true; TestEqual(TEXT("Lost ack remains unknown"), R.Status, ERPGItemBackendStatus::TransportError); }));
	F.Transport->OnSend = {};
	F.Transport->Reply(OriginResponse(F));
	TestTrue(TEXT("Atomic consume path"), F.Transport->Requests.Last().RelativePath.EndsWith(TEXT("/commit")));
	TestTrue(TEXT("Consume body is empty"), F.Transport->Requests.Last().Body.IsEmpty());
	F.Transport->Reply({});
	TestTrue(TEXT("Lost ack callback completed"), bCommitCompleted);
	TestTrue(TEXT("Lost ack retains disk entry"), IFileManager::Get().FileExists(*F.File()));
	F.Subsystem->Deinitialize();
	F.Journal = F.MakeJournal();
	if (!TestTrue(TEXT("Fresh journal restores pending allocation"), F.Configure(true))) return false;
	bool bLoaded = false;
	F.Subsystem->LoadCharacterItems(F.Request.Actor.OwnerId,
		[&](FRPGItemBackendLoadResult R) { bLoaded = true; TestTrue(TEXT("Projection load succeeds after receipt"), R.WasSuccessful()); });
	if (!TestEqual(TEXT("Recovery first verifies consumed origin GET"), F.Transport->Requests.Num(), 3)) return false;
	TestEqual(TEXT("Restart uses only GET"), F.Transport->Requests.Last().Verb, FString(TEXT("GET")));
	TestTrue(TEXT("Origin scope precedes any legacy receipt lookup"), F.Transport->Requests.Last().RelativePath.Contains(TEXT("/pickup-intents/")));
	F.Transport->Reply(OriginResponse(F, true));
	TestEqual(TEXT("Uses original request identity"), F.Transport->Requests.Last().RelativePath,
		TEXT("item-transactions/") + F.Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
	TestFalse(TEXT("No projection load before confirmed receipt"), bLoaded);
	F.Transport->Reply(ReceiptResponse(F.Request));
	TestFalse(TEXT("Verified journal entry removed"), IFileManager::Get().FileExists(*F.File()));
	if (!TestEqual(TEXT("Then loads latest inventory, not stale receipt projection"), F.Transport->Requests.Num(), 5)) return false;
	TestTrue(TEXT("Latest inventory route"), F.Transport->Requests.Last().RelativePath.StartsWith(TEXT("items?")));
	F.Transport->ReplyStatus(200, TEXT("{\"items\":[]}"));
	TestTrue(TEXT("Item load completes"), bLoaded);
	TestEqual(TEXT("No new POST on restart"), F.Transport->Requests.FilterByPredicate(
		[](const auto& R) { return R.Verb == TEXT("POST"); }).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupJournalMissingTest,
	"ProjectRPG.Item.PickupJournal.MissingReceiptBlocksOnlyOriginalOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupJournalMissingTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!F.Journal->Restore() || !F.Journal->Persist(F.Request) || !F.Configure(true)) return false;
	bool bFailed = false;
	F.Subsystem->LoadCharacterItems(F.Request.Actor.OwnerId,
		[&](FRPGItemBackendLoadResult R) { bFailed = !R.WasSuccessful(); });
	F.Transport->ReplyStatus(404);
	TestTrue(TEXT("Missing receipt does not expose Ready projection"), bFailed);
	TestTrue(TEXT("404 keeps original evidence"), IFileManager::Get().FileExists(*F.File()));
	TestFalse(TEXT("Pending owner's new pickup cannot post"), F.Subsystem->Commit(F.Request, [](auto) {}));
	auto Changed = F.Request;
	Changed.Operation = TEXT("Item.Consume");
	TestFalse(TEXT("Pending owner's other mutations also blocked"), F.Subsystem->Commit(Changed, [](auto) {}));
	TestEqual(TEXT("Only original receipt GET sent"), F.Transport->Requests.Num(), 1);
	bool bOtherLoaded = false;
	F.Subsystem->LoadCharacterItems(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower),
		[&](FRPGItemBackendLoadResult R) { bOtherLoaded = R.WasSuccessful(); });
	TestTrue(TEXT("Another member loads without querying original owner's receipt"), F.Transport->Requests.Last().RelativePath.StartsWith(TEXT("items?")));
	F.Transport->ReplyStatus(200, TEXT("{\"items\":[]}"));
	TestTrue(TEXT("Other owner's projection remains available"), bOtherLoaded);
	TestEqual(TEXT("No POST anywhere in restart recovery"), F.Transport->Requests.FilterByPredicate(
		[](const auto& R) { return R.Verb == TEXT("POST"); }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupJournalStaleTest,
	"ProjectRPG.Item.PickupJournal.MalformedAuthAndLateResponsesRetainEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupJournalStaleTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!F.Journal->Restore() || !F.Journal->Persist(F.Request) || !F.Configure(true)) return false;
	bool bFailed = false;
	auto Load = [&]() { bFailed = false; F.Subsystem->LoadCharacterItems(F.Request.Actor.OwnerId,
		[&](FRPGItemBackendLoadResult R) { bFailed = !R.WasSuccessful(); }); };
	Load();
	F.Transport->ReplyStatus(403);
	TestTrue(TEXT("Authentication rejection cannot resolve unknown command"), bFailed);
	Load();
	auto Changed = F.Request;
	Changed.RequestId = FGuid::NewGuid();
	F.Transport->Reply(ReceiptResponse(Changed));
	TestTrue(TEXT("Wrong request response rejected"), bFailed);
	Load();
	Changed = F.Request;
	++Changed.AffectedQuantity;
	F.Transport->Reply(ReceiptResponse(Changed));
	TestTrue(TEXT("Incorrect successful quantity rejected"), bFailed);
	Load();
	const auto Non200 = ReceiptResponse(F.Request);
	F.Transport->ReplyStatus(403, Non200.Body);
	TestTrue(TEXT("Non-200 cannot smuggle a successful receipt"), bFailed);
	Load();
	F.Subsystem->Deinitialize();
	F.Transport->Reply(ReceiptResponse(F.Request));
	TestTrue(TEXT("Late response after teardown fails load"), bFailed);
	TestTrue(TEXT("Teardown/late response does not remove durable request"), IFileManager::Get().FileExists(*F.File()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupJournalDiskFailureTest,
	"ProjectRPG.Item.PickupJournal.PersistenceFailurePreventsHttpSend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupJournalDiskFailureTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!F.Configure(false)) return false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(F.Directory), true);
	FFileHelper::SaveStringToFile(TEXT("fixture-blocks-directory"), *F.Directory);
	bool bRejected = false;
	TestFalse(TEXT("Unwritable journal prevents submission"), F.Subsystem->CommitPickup(F.Drop, F.Request,
		[&](FRPGItemBackendCommitResult R) { bRejected = !R.WasSuccessful(); }));
	TestTrue(TEXT("Caller receives failure"), bRejected);
	TestEqual(TEXT("No HTTP request sent before persistence succeeds"), F.Transport->Requests.Num(), 0);
	TestFalse(TEXT("Journal failure disables new item writes"), F.Subsystem->IsAvailable());
	TestFalse(TEXT("Subsequent command cannot bypass failed journal"), F.Subsystem->CommitPickup(F.Drop, F.Request, [](auto) {}));
	TestEqual(TEXT("Still no HTTP write"), F.Transport->Requests.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPreparedJournalIdentityTest,
	"ProjectRPG.Item.PickupJournal.PreparedIdentitySurvivesAndNeverReleasesOnConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPreparedJournalIdentityTest::RunTest(const FString&)
{
	using namespace RPGPickupJournalTests;
	FFixture F;
	if (!F.Journal->Restore() || !TestTrue(TEXT("V2 persist"), F.Journal->Persist(F.Request, F.Drop))) return false;
	TestTrue(TEXT("Same origin retry"), F.Journal->Persist(F.Request, F.Drop));
	TestFalse(TEXT("Cannot change drop"), F.Journal->Persist(F.Request, FGuid::NewGuid()));
	TestFalse(TEXT("Cannot downgrade to legacy"), F.Journal->Persist(F.Request));
	auto Another = F.Request; Another.RequestId = FGuid::NewGuid();
	TestFalse(TEXT("Drop cannot bind another request"), F.Journal->Persist(Another, F.Drop));
	const auto Restarted = F.MakeJournal();
	TestTrue(TEXT("V2 restart"), Restarted->Restore());
	TestEqual(TEXT("Stable drop identity"), Restarted->GetDropId(F.Request.RequestId), F.Drop);
	auto Rejected = MakeReceipt(F.Request); Rejected.Status = ERPGItemBackendStatus::LocationConflict; Rejected.Records.Reset();
	TestFalse(TEXT("Known rejection never deletes prepared journal"), Restarted->Resolve(Rejected));
	TestTrue(TEXT("Evidence kept"), IFileManager::Get().FileExists(*F.File()));
	FFixture Legacy; Legacy.Journal->Restore(); Legacy.Journal->Persist(Legacy.Request);
	TestFalse(TEXT("Existing v1 cannot be silently adopted into v2"), Legacy.Journal->Persist(Legacy.Request, Legacy.Drop));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPreparedGatewayRetryTest,
	"ProjectRPG.Item.PickupJournal.PreparedGatewayRetriesExactStagesAndRequiresHttp200",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPreparedGatewayRetryTest::RunTest(const FString&)
{
	using namespace RPGPickupJournalTests;
	FFixture F; FGuid Session; FGuid::Parse(F.Session, Session);
	FRPGItemBackendGateway Gateway(F.Transport, 2); int32 Completions = 0;
	Gateway.CommitPickup(F.Server, Session, F.Drop, F.Request, [&](auto Result)
		{ ++Completions; TestTrue(TEXT("Exact success"), Result.WasSuccessful()); });
	F.Transport->Reply({});
	TestEqual(TEXT("Prepare retry body is identical"), F.Transport->Requests[0].Body, F.Transport->Requests[1].Body);
	F.Transport->Reply(OriginResponse(F));
	TestEqual(TEXT("Consume after valid prepare"), F.Transport->Requests.Num(), 3);
	TestTrue(TEXT("Empty consume body"), F.Transport->Requests.Last().Body.IsEmpty());
	F.Transport->Reply({});
	TestEqual(TEXT("Consume retry path unchanged"), F.Transport->Requests[2].RelativePath, F.Transport->Requests[3].RelativePath);
	TestTrue(TEXT("Retry consume body stays empty"), F.Transport->Requests[3].Body.IsEmpty());
	F.Transport->Reply(ReceiptResponse(F.Request)); TestEqual(TEXT("Exactly one completion"), Completions, 1);
	FFixture Denied; Denied.Configure(false);
	Denied.Subsystem->CommitPickup(Denied.Drop, Denied.Request, [&](auto R) { TestEqual(TEXT("Non-200 success body denied"), R.Status, ERPGItemBackendStatus::Forbidden); });
	Denied.Transport->Reply(OriginResponse(Denied));
	Denied.Transport->ReplyStatus(403, ReceiptResponse(Denied.Request).Body);
	TestTrue(TEXT("Denied response retains journal"), IFileManager::Get().FileExists(*Denied.File()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPreparedGatewayMismatchTest,
	"ProjectRPG.Item.PickupJournal.PreparedOriginFullBodyAndScopeMustMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPreparedGatewayMismatchTest::RunTest(const FString&)
{
	using namespace RPGPickupJournalTests;
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		FFixture F; F.Configure(false); bool bRejected = false;
		F.Subsystem->CommitPickup(F.Drop, F.Request, [&](auto R) { bRejected = R.Status == ERPGItemBackendStatus::ProtocolError; });
		auto Response = OriginResponse(F); TSharedPtr<FJsonObject> Origin;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Origin);
		if (Mode == 0) Origin->SetStringField(TEXT("serverId"), TEXT("wrong-server"));
		else if (Mode == 1) Origin->SetStringField(TEXT("dropId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		else Origin->GetObjectField(TEXT("command"))->GetArrayField(TEXT("mutations"))[0]->AsObject()
			->GetObjectField(TEXT("newRecord"))->GetObjectField(TEXT("state"))->SetNumberField(TEXT("generationSeed"), 999);
		Response.Body = Serialize(Origin.ToSharedRef()); F.Transport->Reply(MoveTemp(Response));
		TestTrue(TEXT("Origin mismatch rejected"), bRejected);
		TestEqual(TEXT("No consume after mismatch"), F.Transport->Requests.Num(), 1);
		TestTrue(TEXT("Original evidence retained"), IFileManager::Get().FileExists(*F.File()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPreparedContinuationTest,
	"ProjectRPG.Item.PickupJournal.PreparedGuardAndTeardownPreventSecondPost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPreparedContinuationTest::RunTest(const FString&)
{
	using namespace RPGPickupJournalTests;
	for (bool bTeardown : {false, true})
	{
		FFixture F; F.Configure(false); bool bAuthority = true, bRejected = false;
		F.Subsystem->CommitPickup(F.Drop, F.Request, [&](auto R) { bRejected = !R.WasSuccessful(); }, [&]() { return bAuthority; });
		if (bTeardown) F.Subsystem->Deinitialize(); else bAuthority = false;
		F.Transport->Reply(OriginResponse(F));
		TestTrue(TEXT("Late prepare cannot authorize second write"), bRejected);
		TestEqual(TEXT("Only first prepare sent"), F.Transport->Requests.Num(), 1);
		TestTrue(TEXT("Unknown journal retained"), IFileManager::Get().FileExists(*F.File()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPreparedRecoveryNoReplayTest,
	"ProjectRPG.Item.PickupJournal.PreparedUnconsumedOriginNeverReplaysOrReadsGenericReceipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPreparedRecoveryNoReplayTest::RunTest(const FString&)
{
	using namespace RPGPickupJournalTests;
	FFixture F; F.Journal->Restore(); F.Journal->Persist(F.Request, F.Drop); F.Configure(true);
	bool bBlocked = false;
	F.Subsystem->LoadCharacterItems(F.Request.Actor.OwnerId, [&](auto R) { bBlocked = !R.WasSuccessful(); });
	F.Transport->Reply(OriginResponse(F));
	TestTrue(TEXT("Prepared-only origin blocks recovery"), bBlocked);
	TestEqual(TEXT("Only scope origin GET, no receipt/commit request"), F.Transport->Requests.Num(), 1);
	TestEqual(TEXT("No write on restored origin"), F.Transport->Requests[0].Verb, FString(TEXT("GET")));
	TestTrue(TEXT("File kept"), IFileManager::Get().FileExists(*F.File()));
	TestFalse(TEXT("Generic native pickup entry cannot bypass drop identity"), F.Subsystem->Commit(F.Request, [](auto) {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPreparedRecoveryTamperTest,
	"ProjectRPG.Item.PickupJournal.PreparedMainTmpDropConflictAndDowngradeFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPreparedRecoveryTamperTest::RunTest(const FString&)
{
	using namespace RPGPickupJournalTests;
	for (bool bDowngrade : {false, true})
	{
		FFixture F; F.Journal->Restore(); F.Journal->Persist(F.Request, F.Drop);
		FString Text; FFileHelper::LoadFileToString(Text, *F.File());
		TSharedPtr<FJsonObject> Root; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		if (bDowngrade) Root->SetNumberField(TEXT("schemaVersion"), 1);
		else Root->SetStringField(TEXT("dropId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		FFileHelper::SaveStringToFile(Serialize(Root.ToSharedRef()), *(F.File() + TEXT(".tmp")));
		const auto Restored = F.MakeJournal();
		TestFalse(TEXT("Conflicting/downgraded descriptor fails restore"), Restored->Restore());
		TestEqual(TEXT("No partial restore"), Restored->GetPending().Num(), 0);
		TestTrue(TEXT("Main file kept"), IFileManager::Get().FileExists(*F.File()));
		TestTrue(TEXT("Tmp evidence kept"), IFileManager::Get().FileExists(*(F.File() + TEXT(".tmp"))));
	}
	return true;
}

#endif
