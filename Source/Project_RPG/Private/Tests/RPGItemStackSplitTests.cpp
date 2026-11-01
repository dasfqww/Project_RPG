#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Persistence/RPGInMemoryItemRepository.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Projection/RPGInventoryProjectionStore.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "RPGItemTags.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"
#include "UI/Inventory/Projection/RPGItemIdDragDropOperation.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

namespace RPGItemStackSplitTests
{
struct FFixture
{
	FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, TEXT("11111111-2222-3333-4444-555555555555")};
	FPrimaryAssetId DefinitionId{TEXT("RPGItemDefinition"), TEXT("SplitPotion")};
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	FRPGInventoryProjectionStore Store;
	FRPGItemRecord Source;
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	FFixture()
	{
		Definitions.RegisterSnapshot({DefinitionId, 1, 10});
		Source = MakeRecord(7, 0);
		Load({Source});
	}
	~FFixture()
	{
		if (Model) Model->InitializeFromPlayerController(nullptr);
		if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	FRPGItemRecord MakeRecord(int32 Quantity, int32 Slot, FGuid Id = FGuid(), FRPGItemRecordMetadata Metadata = {},
		FGameplayTagContainer Tags = {}, TArray<FRPGItemStatValue> Stats = {}) const
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(Id.IsValid() ? Id : FGuid::NewGuid(), 37, Quantity, Tags, Stats, State);
		FRPGItemLocation Location{ERPGItemContainerType::Inventory, Owner.OwnerId, Slot};
		FRPGItemRecord Record;
		FRPGItemRecord::TryRestore(DefinitionId, 1, Owner, Location, State, 1, ERPGItemLifecycleState::Active, Metadata, Record);
		return Record;
	}
	bool Load(const TArray<FRPGItemRecord>& Records)
	{
		TArray<FRPGInventoryProjectionEntry> Entries;
		return Store.Replace(Owner, Records, Entries);
	}
	FRPGItemSplitRequest Request() const
	{
		FRPGItemSplitRequest Result;
		Result.RequestId = FGuid::NewGuid(); Result.Actor = Owner;
		Result.ItemId = Source.GetItemId(); Result.ExpectedRevision = Source.GetRevision();
		Result.Destination = {ERPGItemContainerType::Inventory, Owner.OwnerId, 3}; Result.Quantity = 3;
		return Result;
	}
	FRPGItemCommandPlan Plan(const FRPGItemSplitRequest& Request) const
	{ return FRPGItemActionCommandPlanner(Store, Definitions, Actions).Prepare(Request); }
	bool View()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>();
		if (!Controller) return false;
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection); Projection->RegisterComponent();
		auto* Commands = NewObject<URPGItemCommandComponent>(Controller);
		Controller->AddInstanceComponent(Commands); Commands->RegisterComponent();
		Model = NewObject<URPGInventoryProjectionViewModel>(Controller);
		Model->InitializeFromPlayerController(Controller);
		return Projection->ApplyAuthoritativeRecords(Owner, {Source}) && Projection->ApplyCompletedRewardSnapshot(
			Owner, {Source}, Projection->GetAuthoritativeSnapshotGeneration());
	}
};
struct FPipeline final : IRPGItemAsyncCommitter, IRPGItemCommitSink, IRPGItemFirstCommitSink
{
	FRPGInventoryProjectionStore& Store;
	int32 Commits = 0, FirstEffects = 0;
	explicit FPipeline(FRPGInventoryProjectionStore& InStore) : Store(InStore) {}
	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion) override
	{
		++Commits;
		FRPGItemBackendCommitResult Result;
		Result.Status = ERPGItemBackendStatus::Succeeded;
		Result.RequestId = Request.RequestId; Result.Operation = Request.Operation;
		Result.Actor = Request.Actor; Result.CommandFingerprint = Request.CommandFingerprint;
		Result.AffectedQuantity = Request.AffectedQuantity;
		for (const auto& Mutation : Request.Mutations)
			Result.Records.Add(Mutation.NewRecord.CopyWithRevision(Mutation.ExpectedRevision + 1));
		Completion(MoveTemp(Result)); return true;
	}
	virtual bool ApplyCommit(const FRPGItemOwnerRef& Owner, const FRPGItemBackendCommitResult& Result, FString& Error) override
	{
		TArray<FRPGInventoryProjectionEntry> Entries;
		return Store.ApplyMutations(Owner, Result.Records, Entries, &Error);
	}
	virtual bool EnqueueFirstCommit(const FRPGItemBackendCommitResult&, FString&) override { ++FirstEffects; return true; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackSplitPlanTest, "ProjectRPG.Item.StackSplit.PreservesStateAndStableIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackSplitPlanTest::RunTest(const FString&)
{
	using namespace RPGItemStackSplitTests;
	FFixture F;
	FRPGItemRecordMetadata Metadata;
	Metadata.BindState = ERPGItemBindState::CharacterBound; Metadata.Durability = {8, 10};
	Metadata.ExpiresAtUtc = FDateTime::MaxValue(); Metadata.CreationSource = TEXT("Fixture.Original");
	FGameplayTagContainer Tags; Tags.AddTag(RPGGameplayTags::GameItem_Equipment_Helm_Default);
	FRPGItemStatValue Stat; Stat.StatTag = RPGGameplayTags::GameItem_Equipment_Helm_Default; Stat.Value = 2.5f;
	F.Source = F.MakeRecord(7, 0, FGuid(), Metadata, Tags, {Stat}).CopyWithRevision(9007199254740993LL);
	F.Load({F.Source});
	const auto Request = F.Request();
	const auto Plan = F.Plan(Request);
	if (!TestTrue(TEXT("Definition-only planner prepares split"), Plan.IsReady())) return false;
	TestEqual(TEXT("Single CAS has decrement and insert"), Plan.CommitRequest.Mutations.Num(), 2);
	const auto& Remaining = Plan.CommitRequest.Mutations[0].NewRecord;
	const auto& Created = Plan.CommitRequest.Mutations[1].NewRecord;
	TestEqual(TEXT("Original ID retained"), Remaining.GetItemId(), F.Source.GetItemId());
	TestEqual(TEXT("Original location retained"), Remaining.GetLocation().SlotIndex, 0);
	TestEqual(TEXT("Remainder"), Remaining.GetQuantity(), 4);
	TestEqual(TEXT("Split quantity"), Created.GetQuantity(), 3);
	TestEqual(TEXT("Quantity conserved"), Remaining.GetQuantity() + Created.GetQuantity(), 7);
	TestEqual(TEXT("Exact int64 CAS revision"), Plan.CommitRequest.Mutations[0].ExpectedRevision, F.Source.GetRevision());
	TestEqual(TEXT("New record requires absence"), Plan.CommitRequest.Mutations[1].ExpectedRevision, int64(0));
	TestEqual(TEXT("Uncommitted new revision is zero"), Created.GetRevision(), int64(0));
	TestEqual(TEXT("Same request yields same created ID"), F.Plan(Request).CommitRequest.Mutations[1].NewRecord.GetItemId(), Created.GetItemId());
	auto Other = Request; Other.RequestId = FGuid::NewGuid();
	TestNotEqual(TEXT("Different request has a different created ID"), F.Plan(Other).CommitRequest.Mutations[1].NewRecord.GetItemId(), Created.GetItemId());
	TestEqual(TEXT("Original seed retained"), Created.GetState().GetGenerationSeed(), F.Source.GetState().GetGenerationSeed());
	TestTrue(TEXT("Original tags retained"), Created.GetState().GetInstanceTags() == Tags);
	TestEqual(TEXT("Original roll retained"), Created.GetState().GetStatValues()[0].Value, Stat.Value);
	TestEqual(TEXT("Binding retained"), Created.GetMetadata().BindState, Metadata.BindState);
	TestEqual(TEXT("Durability retained"), Created.GetMetadata().Durability.Current, 8);
	TestTrue(TEXT("Expiry retained"), Created.GetMetadata().ExpiresAtUtc == Metadata.ExpiresAtUtc);
	TestEqual(TEXT("Audit origin retained"), Created.GetMetadata().CreationSource, Metadata.CreationSource);
	TestFalse(TEXT("Both stacks remain unlocked active records"), Created.GetMetadata().bLocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackSplitGuardTest, "ProjectRPG.Item.StackSplit.RejectsInvalidAuthorityQuantityAndCollisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackSplitGuardTest::RunTest(const FString&)
{
	using namespace RPGItemStackSplitTests;
	FFixture F;
	for (int32 Quantity : {0, -1, 7, 8, MAX_int32})
	{
		auto Request = F.Request(); Request.Quantity = Quantity;
		TestFalse(TEXT("No zero, negative, full-source or overflow split"), F.Plan(Request).IsReady());
	}
	auto Request = F.Request(); Request.Actor.OwnerId = TEXT("another-owner"); Request.Destination.ContainerId = Request.Actor.OwnerId;
	TestEqual(TEXT("Foreign owner rejected"), F.Plan(Request).FailureStatus, ERPGItemTransactionStatus::Forbidden);
	Request = F.Request(); ++Request.ExpectedRevision;
	TestEqual(TEXT("Stale source rejected"), F.Plan(Request).FailureStatus, ERPGItemTransactionStatus::RevisionConflict);
	for (int32 Slot : {-1, 80, MAX_int32}) { Request = F.Request(); Request.Destination.SlotIndex = Slot; TestFalse(TEXT("Out of bag rejected"), F.Plan(Request).IsReady()); }
	Request = F.Request(); Request.Destination.ContainerId = TEXT("another-bag");
	TestFalse(TEXT("Cross-container split rejected"), F.Plan(Request).IsReady());
	Request = F.Request(); Request.Destination.SlotIndex = 0;
	TestFalse(TEXT("Same source slot rejected"), F.Plan(Request).IsReady());
	F.Load({F.Source, F.MakeRecord(1, 3)});
	TestEqual(TEXT("Occupied destination rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::LocationOccupied);
	F.Load({F.Source}); Request = F.Request();
	const auto Collision = F.MakeRecord(1, 4, FRPGItemActionCommandPlanner::MakeSplitItemId(Request.RequestId, Request.ItemId));
	FRPGItemRecord Tombstone; Collision.TryMarkConsumed(Tombstone);
	F.Load({F.Source, Tombstone});
	TestEqual(TEXT("An existing terminal ID is never reused"), F.Plan(Request).FailureStatus, ERPGItemTransactionStatus::RepositoryRejected);
	FRPGItemRecordMetadata Metadata; Metadata.bLocked = true;
	F.Source = F.MakeRecord(7, 0, FGuid(), Metadata); F.Load({F.Source});
	TestEqual(TEXT("Locked source rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::Locked);
	Metadata = {}; Metadata.ExpiresAtUtc = FDateTime(2000,1,1);
	F.Source = F.MakeRecord(7, 0, FGuid(), Metadata); F.Load({F.Source});
	TestEqual(TEXT("Expired source rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::Expired);
	F.Source = F.MakeRecord(7, 0); F.Load({F.Source}); F.Definitions.RegisterSnapshot({F.DefinitionId, 2, 10});
	TestEqual(TEXT("Definition version checked"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::DefinitionVersionMismatch);
	F.Definitions.RegisterSnapshot({F.DefinitionId, 1, 1});
	TestEqual(TEXT("Nonstackable definition rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::IncompatibleStack);
	F.Definitions.RegisterSnapshot({F.DefinitionId, 1, 5});
	TestFalse(TEXT("Over-cap source cannot mint split stacks"), F.Plan(F.Request()).IsReady());
	F.Definitions.Reset();
	TestEqual(TEXT("Missing definition rejected"), F.Plan(F.Request()).FailureStatus, ERPGItemTransactionStatus::DefinitionUnavailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackSplitCASTest, "ProjectRPG.Item.StackSplit.AtomicCompareAndSwap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackSplitCASTest::RunTest(const FString&)
{
	using namespace RPGItemStackSplitTests;
	FFixture F;
	const auto Request = F.Request(); const auto Plan = F.Plan(Request);
	if (!TestTrue(TEXT("Plan ready"), Plan.IsReady())) return false;
	FRPGInMemoryItemRepository Repository;
	FRPGItemRepositoryCommitRequest Seed;
	Seed.RequestId = FGuid::NewGuid(); Seed.Actor = F.Owner; Seed.Operation = TEXT("Fixture.Seed"); Seed.CommandFingerprint = TEXT("source");
	Seed.Mutations.Add({0, F.Source.CopyWithRevision(0)});
	TestTrue(TEXT("Seed source"), Repository.Commit(Seed).WasApplied());
	auto Stale = Plan.CommitRequest;
	Stale.Mutations[0].ExpectedRevision = 2; Stale.Mutations[0].NewRecord = Stale.Mutations[0].NewRecord.CopyWithRevision(2);
	TestFalse(TEXT("Stale CAS rejects whole split"), Repository.Commit(Stale).WasApplied());
	FRPGItemRecord Stored;
	TestFalse(TEXT("Failed decrement cannot insert a new stack"), Repository.Find(Plan.CommitRequest.Mutations[1].NewRecord.GetItemId(), Stored));
	Repository.Find(F.Source.GetItemId(), Stored); TestEqual(TEXT("Failure retains original quantity"), Stored.GetQuantity(), 7);
	TestTrue(TEXT("Matching CAS commits both"), Repository.Commit(Plan.CommitRequest).WasApplied());
	Repository.Find(F.Source.GetItemId(), Stored); TestEqual(TEXT("Source after commit"), Stored.GetQuantity(), 4);
	Repository.Find(Plan.CommitRequest.Mutations[1].NewRecord.GetItemId(), Stored); TestEqual(TEXT("Created after commit"), Stored.GetQuantity(), 3);
	TestEqual(TEXT("Identical commit reuses receipt"), Repository.Commit(Plan.CommitRequest).Status, ERPGItemRepositoryCommitStatus::AlreadyCommitted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackSplitReplayTest, "ProjectRPG.Item.StackSplit.AsyncProjectionAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackSplitReplayTest::RunTest(const FString&)
{
	using namespace RPGItemStackSplitTests;
	FFixture F; FPipeline Adapters(F.Store);
	const auto Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(
		F.Store, F.Definitions, F.Actions, Adapters, Adapters, Adapters);
	auto Request = F.Request(); FRPGItemAsyncCommandOutcome Outcome;
	const auto Completion = [&Outcome](FRPGItemAsyncCommandOutcome Value) { Outcome = MoveTemp(Value); };
	TestTrue(TEXT("Async split submits"), Pipeline->Split(Request, Completion)); TestTrue(TEXT("Async split completes"), Outcome.WasSuccessful());
	FRPGItemRecord Stored; F.Store.Find(F.Source.GetItemId(), Stored); TestEqual(TEXT("Source reconciled"), Stored.GetQuantity(), 4);
	const auto CreatedId = FRPGItemActionCommandPlanner::MakeSplitItemId(Request.RequestId, Request.ItemId);
	TestTrue(TEXT("New identity reconciled"), F.Store.Find(CreatedId, Stored)); TestEqual(TEXT("New committed revision"), Stored.GetRevision(), int64(1));
	Pipeline->Split(Request, Completion); TestEqual(TEXT("Old source revision can replay cached receipt"), Outcome.BackendResult.Status, ERPGItemBackendStatus::AlreadyApplied);
	TestEqual(TEXT("Replay makes no second commit"), Adapters.Commits, 1);
	Request.Quantity = 2; Pipeline->Split(Request, Completion);
	TestEqual(TEXT("Same ID with different quantity conflicts"), Outcome.PlanFailureStatus, ERPGItemTransactionStatus::IdempotencyConflict);
	TestEqual(TEXT("Conflict mints nothing"), Adapters.Commits, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackSplitReservationTest, "ProjectRPG.Item.StackSplit.WaitForSourceAndExactCreatedItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackSplitReservationTest::RunTest(const FString&)
{
	using namespace RPGItemStackSplitTests;
	FFixture F; if (!TestTrue(TEXT("View fixture"), F.View())) return false;
	const auto Request = F.Request(); const auto Plan = F.Plan(Request);
	if (!TestTrue(TEXT("Plan ready"), Plan.IsReady())) return false;
	const auto Created = Plan.CommitRequest.Mutations[1].NewRecord.CopyWithRevision(1);
	const auto Remaining = Plan.CommitRequest.Mutations[0].NewRecord.CopyWithRevision(2);
	const auto Reserve = [&]()
	{
		auto& Pending = F.Model->PendingInventoryMovesByRequest.Add(Request.RequestId);
		Pending.ItemId = F.Source.GetItemId(); Pending.ExpectedRevision = 1; Pending.SourceSlot = 0; Pending.DestinationSlot = 3;
		Pending.SwapItemId = Created.GetItemId(); Pending.bWaitForCreatedItem = true;
	};
	Reserve(); FRPGItemCommandClientResult Ack; Ack.RequestId = Request.RequestId; Ack.Result = ERPGItemCommandResultCode::Succeeded;
	F.Model->HandleCommandCompleted(Ack);
	TestTrue(TEXT("Success ACK alone holds destination"), F.Model->IsInventorySlotReserved(3));
	F.Projection->ApplyAuthoritativeRecords(F.Owner, {Remaining});
	TestTrue(TEXT("Source-only update cannot release an absent new stack"), F.Model->IsInventorySlotReserved(3));
	F.Projection->ApplyAuthoritativeRecords(F.Owner, {Remaining, F.MakeRecord(3, 3)});
	TestTrue(TEXT("Wrong ID in same slot does not release"), F.Model->IsInventorySlotReserved(3));
	F.Projection->ApplyAuthoritativeRecords(F.Owner, {Remaining, Created});
	TestFalse(TEXT("Exact new ID and source update release"), F.Model->IsInventorySlotReserved(3));
	F.Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source}); Reserve();
	F.Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source, Created});
	TestTrue(TEXT("New stack first still holds source"), F.Model->IsInventorySlotReserved(0));
	F.Projection->ApplyAuthoritativeRecords(F.Owner, {Remaining, Created});
	TestFalse(TEXT("Reversed order releases after both"), F.Model->IsInventorySlotReserved(0));
	Reserve(); Ack.Result = ERPGItemCommandResultCode::Conflict; F.Model->HandleCommandCompleted(Ack);
	TestFalse(TEXT("Explicit failure releases destination"), F.Model->IsInventorySlotReserved(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemStackSplitInputTest, "ProjectRPG.Item.StackSplit.ShiftDragAndBlueprintRequestBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemStackSplitInputTest::RunTest(const FString&)
{
	using namespace RPGItemStackSplitTests;
	FFixture F; if (!TestTrue(TEXT("View fixture"), F.View())) return false;
	auto* SourceCell = NewObject<URPGInventoryProjectionItemSlotWidget>(F.Controller);
	auto* EmptyCell = NewObject<URPGInventoryProjectionItemSlotWidget>(F.Controller);
	auto* OccupiedCell = NewObject<URPGInventoryProjectionItemSlotWidget>(F.Controller);
	SourceCell->SetItem(F.Source.GetItemId(), FText::FromString(TEXT("Potion")), F.Model, false, 0);
	EmptyCell->SetItem(FGuid(), FText::GetEmpty(), F.Model, false, 3);
	OccupiedCell->SetItem(F.Source.GetItemId(), FText::GetEmpty(), F.Model, false, 0);
	const FPointerEvent Pointer(0, FSlateApplication::CursorPointerIndex, FVector2D::ZeroVector, FVector2D::ZeroVector,
		TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.f, FModifierKeysState(true,false,false,false,false,false,false,false,false));
	UDragDropOperation* Operation = nullptr; SourceCell->NativeOnDragDetected(FGeometry(), Pointer, Operation);
	auto* Drag = Cast<URPGItemIdDragDropOperation>(Operation);
	if (!TestNotNull(TEXT("Native split drag payload"), Drag)) return false;
	TestTrue(TEXT("Shift intent captured"), Drag->bSplitStack); TestEqual(TEXT("Odd stack splits floor half"), Drag->SplitQuantity, 3);
	TestFalse(TEXT("Occupied target cannot fall back to merge/swap"), OccupiedCell->NativeOnDrop(FGeometry(), FDragDropEvent(Pointer,nullptr), Drag));
	TestFalse(TEXT("Rejected target submits nothing"), F.Model->bHasLastCommandResult);
	TestTrue(TEXT("Empty target submits via native ViewModel"), EmptyCell->NativeOnDrop(FGeometry(), FDragDropEvent(Pointer,nullptr), Drag));
	TestEqual(TEXT("Facade uses Split, not whole-stack Move"), F.Model->LastCommandResult.Operation, FName(TEXT("Item.Split")));
	FRPGInventoryProjectionEntry Entry; F.Projection->FindProjectedItem(F.Source.GetItemId(), Entry);
	TestEqual(TEXT("UI never changes authoritative quantity optimistically"), Entry.GetQuantity(), 7);
	TestFalse(TEXT("Full-source BP split rejected"), F.Model->CanSplitItem(F.Source.GetItemId(), 1, 3, 7));
	TestTrue(TEXT("BP can choose one item"), F.Model->CanSplitItem(F.Source.GetItemId(), 1, 3, 1));
	TestFalse(TEXT("Stale BP revision rejected"), F.Model->CanSplitItem(F.Source.GetItemId(), 2, 3, 1));
	return true;
}
#endif
