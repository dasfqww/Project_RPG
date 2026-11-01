#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Persistence/RPGInMemoryItemRepository.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Projection/RPGInventoryProjectionStore.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "RPGItemTags.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

namespace RPGItemStackMergeTests
{
struct FFixture
{
	FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, TEXT("11111111-2222-3333-4444-555555555555")};
	FPrimaryAssetId DefinitionId{TEXT("RPGItemDefinition"), TEXT("MergePotion")};
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	FRPGInventoryProjectionStore Store;
	FRPGItemRecord Source;
	FRPGItemRecord Destination;
	FRPGItemDefinitionSnapshot Definition;

	FFixture()
	{
		Definition = {DefinitionId, 1, 10};
		Definitions.RegisterSnapshot(Definition);
		Source = MakeRecord(0, 7);
		Destination = MakeRecord(1, 8);
		Load();
	}
	FRPGItemRecord MakeRecord(int32 Slot, int32 Quantity, FRPGItemRecordMetadata Metadata = {},
		FGameplayTagContainer Tags = {}, TArray<FRPGItemStatValue> Stats = {}) const
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 31, Quantity, Tags, Stats, State);
		FRPGItemLocation Location{ERPGItemContainerType::Inventory, Owner.OwnerId, Slot};
		FRPGItemRecord Record;
		FRPGItemRecord::TryRestore(DefinitionId, 1, Owner, Location, State,
			9007199254740993LL, ERPGItemLifecycleState::Active, Metadata, Record);
		return Record;
	}
	bool Load()
	{
		TArray<FRPGInventoryProjectionEntry> Entries;
		return Store.Replace(Owner, {Source, Destination}, Entries);
	}
	FRPGItemStackTransferRequest Request() const
	{
		FRPGItemStackTransferRequest Result;
		Result.RequestId = FGuid::NewGuid();
		Result.Actor = Owner;
		Result.SourceItemId = Source.GetItemId();
		Result.ExpectedSourceRevision = Source.GetRevision();
		Result.DestinationItemId = Destination.GetItemId();
		Result.ExpectedDestinationRevision = Destination.GetRevision();
		Result.RequestedQuantity = MAX_int32;
		return Result;
	}
	FRPGItemCommandPlan Plan(const FRPGItemStackTransferRequest& Request) const
	{
		return FRPGItemActionCommandPlanner(Store, Definitions, Actions).Prepare(Request);
	}
};

class FCommitter final : public IRPGItemAsyncCommitter
{
public:
	int32 Count = 0;
	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) override
	{
		++Count;
		FRPGItemBackendCommitResult Result;
		Result.Status = ERPGItemBackendStatus::Succeeded;
		Result.RequestId = Request.RequestId;
		Result.Operation = Request.Operation;
		Result.Actor = Request.Actor;
		Result.CommandFingerprint = Request.CommandFingerprint;
		Result.AffectedQuantity = Request.AffectedQuantity;
		for (const auto& Mutation : Request.Mutations)
			Result.Records.Add(Mutation.NewRecord.CopyWithRevision(Mutation.ExpectedRevision + 1));
		Completion(MoveTemp(Result));
		return true;
	}
};
class FSink final : public IRPGItemCommitSink, public IRPGItemFirstCommitSink
{
public:
	FRPGInventoryProjectionStore& Store;
	int32 FirstCount = 0;
	explicit FSink(FRPGInventoryProjectionStore& InStore) : Store(InStore) {}
	virtual bool ApplyCommit(const FRPGItemOwnerRef& Owner, const FRPGItemBackendCommitResult& Result,
		FString& Error) override
	{
		TArray<FRPGInventoryProjectionEntry> Entries;
		return Store.ApplyMutations(Owner, Result.Records, Entries, &Error);
	}
	virtual bool EnqueueFirstCommit(const FRPGItemBackendCommitResult&, FString&) override
	{
		++FirstCount;
		return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackMergePlanTest,
	"ProjectRPG.Item.StackMerge.PartialFullAndAtomicCAS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackMergePlanTest::RunTest(const FString&)
{
	using namespace RPGItemStackMergeTests;
	FFixture F;
	auto Request = F.Request();
	const auto Plan = F.Plan(Request);
	if (!TestTrue(TEXT("Partial merge planned without an action policy"), Plan.IsReady())) return false;
	TestEqual(TEXT("One CAS covers both records"), Plan.CommitRequest.Mutations.Num(), 2);
	TestEqual(TEXT("Only capacity is transferred"), Plan.CommitRequest.AffectedQuantity, 2);
	TestEqual(TEXT("Remainder stays in source"), Plan.CommitRequest.Mutations[0].NewRecord.GetQuantity(), 5);
	TestEqual(TEXT("Destination reaches cap"), Plan.CommitRequest.Mutations[1].NewRecord.GetQuantity(), 10);
	TestEqual(TEXT("Source identity preserved"), Plan.CommitRequest.Mutations[0].NewRecord.GetItemId(), F.Source.GetItemId());
	TestEqual(TEXT("Source location preserved"), Plan.CommitRequest.Mutations[0].NewRecord.GetLocation().SlotIndex, 0);

	FRPGInMemoryItemRepository Repository;
	FRPGItemRepositoryCommitRequest Seed;
	Seed.RequestId = FGuid::NewGuid(); Seed.Operation = TEXT("Fixture.Seed");
	Seed.CommandFingerprint = TEXT("merge-pair"); Seed.Actor = F.Owner;
	Seed.Mutations.Add({0, F.Source.CopyWithRevision(0)});
	Seed.Mutations.Add({0, F.Destination.CopyWithRevision(0)});
	if (!TestTrue(TEXT("Seed real reference CAS repository"), Repository.Commit(Seed).WasApplied())) return false;
	auto Commit = Plan.CommitRequest;
	for (auto& Mutation : Commit.Mutations)
	{
		Mutation.ExpectedRevision = 1;
		Mutation.NewRecord = Mutation.NewRecord.CopyWithRevision(1);
	}
	Commit.Mutations[1].ExpectedRevision = 2;
	Commit.Mutations[1].NewRecord = Commit.Mutations[1].NewRecord.CopyWithRevision(2);
	TestFalse(TEXT("Stale destination aborts both mutations"), Repository.Commit(Commit).WasApplied());
	FRPGItemRecord Stored;
	Repository.Find(F.Source.GetItemId(), Stored);
	TestEqual(TEXT("Failed CAS never decrements source"), Stored.GetQuantity(), 7);
	Commit.Mutations[1].ExpectedRevision = 1;
	Commit.Mutations[1].NewRecord = Commit.Mutations[1].NewRecord.CopyWithRevision(1);
	TestTrue(TEXT("Matching pair commits atomically"), Repository.Commit(Commit).WasApplied());
	TestEqual(TEXT("Identical request returns original receipt"), Repository.Commit(Commit).Status,
		ERPGItemRepositoryCommitStatus::AlreadyCommitted);

	F.Source = F.MakeRecord(0, 2); F.Destination = F.MakeRecord(1, 4); F.Load();
	const auto Full = F.Plan(F.Request());
	if (!TestTrue(TEXT("Full merge planned"), Full.IsReady())) return false;
	TestEqual(TEXT("Empty source becomes a consumed tombstone"), Full.CommitRequest.Mutations[0].NewRecord.GetLifecycleState(),
		ERPGItemLifecycleState::Consumed);
	TestEqual(TEXT("Destination retains its stable ID"), Full.CommitRequest.Mutations[1].NewRecord.GetItemId(), F.Destination.GetItemId());
	TestEqual(TEXT("Total quantity conserved"), Full.CommitRequest.Mutations[1].NewRecord.GetQuantity(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackMergeGuardsTest,
	"ProjectRPG.Item.StackMerge.AuthorityMetadataAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackMergeGuardsTest::RunTest(const FString&)
{
	using namespace RPGItemStackMergeTests;
	FFixture F;
	auto Request = F.Request();
	Request.Actor.OwnerId = TEXT("another-owner");
	TestEqual(TEXT("Foreign owner rejected"), F.Plan(Request).FailureStatus, ERPGItemTransactionStatus::Forbidden);
	Request = F.Request(); --Request.ExpectedDestinationRevision;
	TestEqual(TEXT("Full int64 stale destination rejected"), F.Plan(Request).FailureStatus, ERPGItemTransactionStatus::RevisionConflict);
	Request = F.Request(); Request.DestinationItemId = Request.SourceItemId;
	TestFalse(TEXT("Self merge rejected"), F.Plan(Request).IsReady());
	Request = F.Request(); Request.RequestedQuantity = 0;
	TestFalse(TEXT("Zero transfer rejected"), F.Plan(Request).IsReady());
	FRPGItemRecordMetadata Metadata;
	Metadata.BindState = ERPGItemBindState::CharacterBound;
	F.Destination = F.MakeRecord(1, 4, Metadata); F.Load();
	TestEqual(TEXT("Binding cannot be washed"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::IncompatibleStack);
	Metadata = {}; Metadata.Durability = {1, 10};
	F.Destination = F.MakeRecord(1, 4, Metadata); F.Load();
	TestEqual(TEXT("Durability mismatch rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::IncompatibleStack);
	Metadata = {}; Metadata.ExpiresAtUtc = FDateTime::MaxValue();
	F.Destination = F.MakeRecord(1, 4, Metadata); F.Load();
	TestEqual(TEXT("Expiry cannot be extended by merge"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::IncompatibleStack);
	Metadata = {}; Metadata.bLocked = true;
	F.Destination = F.MakeRecord(1, 4, Metadata); F.Load();
	TestEqual(TEXT("Locked destination rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::Locked);
	Metadata = {}; Metadata.ExpiresAtUtc = FDateTime(2000, 1, 1);
	F.Destination = F.MakeRecord(1, 4, Metadata); F.Load();
	TestEqual(TEXT("Expired destination rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::Expired);
	F.Destination = F.MakeRecord(1, 10); F.Load();
	TestEqual(TEXT("Full destination rejected, not swapped"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::StackFull);
	F.Destination = F.MakeRecord(80, 4); F.Load();
	TestEqual(TEXT("Slot 80 rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::InvalidContainerTransition);
	F.Destination = F.MakeRecord(1, 11); F.Load();
	TestEqual(TEXT("Corrupt over-cap stack rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::IncompatibleStack);
	FGameplayTagContainer Tags;
	Tags.AddTag(RPGGameplayTags::GameItem_Equipment_Helm_Default);
	F.Destination = F.MakeRecord(1, 4, {}, Tags); F.Load();
	TestEqual(TEXT("Different instance tags rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::IncompatibleStack);
	FRPGItemStatValue Stat;
	Stat.StatTag = RPGGameplayTags::GameItem_Equipment_Helm_Default; Stat.Value = 3.f;
	F.Destination = F.MakeRecord(1, 4, {}, {}, {Stat}); F.Load();
	TestEqual(TEXT("Different rolled stats rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::IncompatibleStack);
	F.Destination = F.MakeRecord(1, 4); F.Load();
	F.Definition.DefinitionVersion = 2; F.Definitions.RegisterSnapshot(F.Definition);
	TestEqual(TEXT("Server definition version must match"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::DefinitionVersionMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackMergeReplayTest,
	"ProjectRPG.Item.StackMerge.AsyncProjectionAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackMergeReplayTest::RunTest(const FString&)
{
	using namespace RPGItemStackMergeTests;
	FFixture F;
	F.Source = F.MakeRecord(0, 2); F.Destination = F.MakeRecord(1, 4); F.Load();
	FCommitter Committer;
	FSink Sink(F.Store);
	const auto Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(
		F.Store, F.Definitions, F.Actions, Committer, Sink, Sink);
	auto Request = F.Request();
	FRPGItemAsyncCommandOutcome Outcome;
	const auto Completion = [&Outcome](FRPGItemAsyncCommandOutcome Value) { Outcome = MoveTemp(Value); };
	TestTrue(TEXT("Async merge submitted"), Pipeline->TransferStack(Request, Completion));
	TestTrue(TEXT("Async merge completed"), Outcome.WasSuccessful());
	FRPGItemRecord Stored;
	F.Store.Find(F.Source.GetItemId(), Stored);
	TestFalse(TEXT("Source removed from active projection"), Stored.IsActive());
	F.Store.Find(F.Destination.GetItemId(), Stored);
	TestEqual(TEXT("Destination projection has total quantity"), Stored.GetQuantity(), 6);
	TestEqual(TEXT("Exact int64 revision advanced"), Stored.GetRevision(), F.Destination.GetRevision() + 1);
	TestTrue(TEXT("Same RPC is replayable after source consumption"), Pipeline->TransferStack(Request, Completion));
	TestEqual(TEXT("Cached response is AlreadyApplied"), Outcome.BackendResult.Status, ERPGItemBackendStatus::AlreadyApplied);
	TestEqual(TEXT("Only one external commit"), Committer.Count, 1);
	TestEqual(TEXT("Only first receipt reaches first-commit sink"), Sink.FirstCount, 1);
	--Request.RequestedQuantity;
	Pipeline->TransferStack(Request, Completion);
	TestEqual(TEXT("Changed intent cannot reuse request ID"), Outcome.PlanFailureStatus, ERPGItemTransactionStatus::IdempotencyConflict);
	TestEqual(TEXT("Conflict makes no external write"), Committer.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackMergeDropTest,
	"ProjectRPG.Item.StackMerge.OccupiedDropRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackMergeDropTest::RunTest(const FString&)
{
	using namespace RPGItemStackMergeTests;
	using EAction = ERPGInventoryOccupiedDropAction;
	FFixture F;
	const auto Route = [&F]()
	{
		TArray<FRPGInventoryProjectionEntry> Entries;
		F.Store.Replace(F.Owner, {F.Source, F.Destination}, Entries);
		return URPGInventoryProjectionViewModel::BuildOccupiedDropAction(Entries[0], Entries[1], &F.Definition);
	};
	TestEqual(TEXT("Matching potion drop merges"), Route(), EAction::Merge);
	F.Destination = F.MakeRecord(1, 10);
	TestEqual(TEXT("Full stack never silently swaps"), Route(), EAction::Rejected);
	FRPGItemRecordMetadata Metadata; Metadata.BindState = ERPGItemBindState::CharacterBound;
	F.Destination = F.MakeRecord(1, 4, Metadata);
	TestEqual(TEXT("Incompatible stack does not send a swap"), Route(), EAction::Rejected);
	F.Source = F.MakeRecord(0, 1); F.Destination = F.MakeRecord(1, 1); F.Definition.MaxStackSize = 1;
	TestEqual(TEXT("Non-stackable same-definition gear still swaps"), Route(), EAction::Swap);
	F.Definition.DefinitionVersion = 2;
	TestEqual(TEXT("Stale local catalog cannot choose merge"), Route(), EAction::Rejected);
	TArray<FRPGInventoryProjectionEntry> Entries;
	F.Store.Replace(F.Owner, {F.Source, F.Destination}, Entries);
	TestEqual(TEXT("Missing same-type catalog is conservative"),
		URPGInventoryProjectionViewModel::BuildOccupiedDropAction(Entries[0], Entries[1], nullptr), EAction::Rejected);
	F.DefinitionId = FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("OtherItem"));
	F.Destination = F.MakeRecord(1, 1);
	TestEqual(TEXT("Different item types retain swap behavior"), Route(), EAction::Swap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackMergeReservationTest,
	"ProjectRPG.Item.StackMerge.DualReservationWaitsForBothSnapshots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackMergeReservationTest::RunTest(const FString&)
{
	using namespace RPGItemStackMergeTests;
	FFixture F;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Reservation world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!Controller) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
	auto* Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
	Controller->AddInstanceComponent(Projection); Projection->RegisterComponent();
	auto* Model = NewObject<URPGInventoryProjectionViewModel>(Controller);
	Model->InitializeFromPlayerController(Controller);
	TestTrue(TEXT("Owned projection loaded"), Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source, F.Destination}));
	TestTrue(TEXT("Completed fixture snapshot is Ready"), Projection->ApplyCompletedRewardSnapshot(F.Owner,
		{F.Source, F.Destination}, Projection->GetAuthoritativeSnapshotGeneration()));
	const FGuid RequestId = FGuid::NewGuid();
	auto& Pending = Model->PendingInventoryMovesByRequest.Add(RequestId);
	Pending.ItemId = F.Source.GetItemId(); Pending.ExpectedRevision = F.Source.GetRevision();
	Pending.SwapItemId = F.Destination.GetItemId(); Pending.ExpectedSwapRevision = F.Destination.GetRevision();
	Pending.SourceSlot = 0; Pending.DestinationSlot = 1;
	FRPGItemCommandClientResult Ack;
	Ack.RequestId = RequestId; Ack.Operation = TEXT("Item.StackTransfer"); Ack.Result = ERPGItemCommandResultCode::Succeeded;
	Model->HandleCommandCompleted(Ack);
	TestTrue(TEXT("Early success ACK keeps both reservations"),
		Model->IsInventorySlotReserved(0) && Model->IsInventorySlotReserved(1));
	TestTrue(TEXT("Source removal arrives first"), Projection->ApplyAuthoritativeRecords(F.Owner, {F.Destination}));
	TestTrue(TEXT("Source removal alone keeps destination locked"), Model->IsInventorySlotReserved(1));
	TestTrue(TEXT("Destination revision arrives"), Projection->ApplyAuthoritativeRecords(F.Owner,
		{F.Destination.CopyWithRevision(F.Destination.GetRevision() + 1)}));
	TestFalse(TEXT("Both snapshots retired releases source"), Model->IsInventorySlotReserved(0));
	TestFalse(TEXT("Both snapshots retired releases destination"), Model->IsInventorySlotReserved(1));
	auto& Rejected = Model->PendingInventoryMovesByRequest.Add(RequestId);
	Rejected.ItemId = F.Source.GetItemId(); Rejected.SwapItemId = F.Destination.GetItemId();
	Rejected.SourceSlot = 0; Rejected.DestinationSlot = 1;
	Ack.Result = ERPGItemCommandResultCode::Conflict; Model->HandleCommandCompleted(Ack);
	TestFalse(TEXT("Explicit failure releases both reservations"), Model->IsInventorySlotReserved(1));
	Model->InitializeFromPlayerController(nullptr);
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

#endif
