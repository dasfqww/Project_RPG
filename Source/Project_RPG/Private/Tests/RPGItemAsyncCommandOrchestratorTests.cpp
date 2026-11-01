#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Projection/RPGInventoryProjectionStore.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"

namespace RPGItemAsyncCommandOrchestratorTests
{
FRPGItemOwnerRef MakeOwner()
{
	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = TEXT("12345678-1234-1234-1234-123456789abc");
	return Owner;
}

FPrimaryAssetId MakeDefinitionId()
{
	return FPrimaryAssetId(
		FPrimaryAssetType(FName(TEXT("RPGItemDefinition"))),
		FName(TEXT("AsyncConsumeDefinition")));
}

bool MakeRecord(
	const FRPGItemOwnerRef& Owner,
	const FGuid& ItemId,
	const int32 Quantity,
	const int64 Revision,
	FRPGItemRecord& OutRecord)
{
	FRPGItemInstanceState State;
	if (!FRPGItemInstanceState::TryRestore(
		ItemId,
		42,
		Quantity,
		{},
		{},
		State))
	{
		return false;
	}
	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Inventory;
	Location.ContainerId = Owner.OwnerId;
	Location.SlotIndex = 0;
	return FRPGItemRecord::TryRestore(
		MakeDefinitionId(),
		1,
		Owner,
		Location,
		State,
		Revision,
		ERPGItemLifecycleState::Active,
		{},
		OutRecord);
}

class FFakeCommitter final : public IRPGItemAsyncCommitter
{
public:
	virtual bool Commit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) override
	{
		++CommitCount;
		LastRequest = Request;
		FRPGItemBackendCommitResult Result;
		Result.Status = ReplyStatus;
		Result.RequestId = bReturnMismatchedReceipt
			? FGuid::NewGuid()
			: Request.RequestId;
		Result.Operation = Request.Operation;
		Result.CommandFingerprint = Request.CommandFingerprint;
		Result.Actor = Request.Actor;
		Result.AffectedQuantity = Request.AffectedQuantity;
		for (const FRPGItemRecordMutation& Mutation : Request.Mutations)
		{
			Result.Records.Add(
				Mutation.NewRecord.CopyWithRevision(
					Mutation.ExpectedRevision + 1));
		}
		if (MutateReceipt) MutateReceipt(Result);
		Completion(MoveTemp(Result));
		return true;
	}

	int32 CommitCount = 0;
	bool bReturnMismatchedReceipt = false;
	ERPGItemBackendStatus ReplyStatus = ERPGItemBackendStatus::Succeeded;
	TFunction<void(FRPGItemBackendCommitResult&)> MutateReceipt;
	FRPGItemRepositoryCommitRequest LastRequest;
};

class FProjectionSink final : public IRPGItemCommitSink
{
public:
	FProjectionSink(
		FRPGInventoryProjectionStore& InStore,
		const FRPGItemOwnerRef& InOwner)
		: Store(InStore)
		, Owner(InOwner)
	{
	}

	virtual bool ApplyCommit(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& Result,
		FString& OutError) override
	{
		++ApplyCount;
		return ExpectedOwner == Owner && Store.ApplyMutations(
			Owner,
			Result.Records,
			LastEntries,
			&OutError);
	}

	FRPGInventoryProjectionStore& Store;
	FRPGItemOwnerRef Owner;
	int32 ApplyCount = 0;
	TArray<FRPGInventoryProjectionEntry> LastEntries;
};

class FFirstCommitSink final : public IRPGItemFirstCommitSink
{
public:
	virtual bool EnqueueFirstCommit(
		const FRPGItemBackendCommitResult& Result,
		FString& OutError) override
	{
		++ApplyCount;
		LastRequestId = Result.RequestId;
		return true;
	}

	int32 ApplyCount = 0;
	FGuid LastRequestId;
};

void RegisterConsumePolicy(
	FRPGItemDefinitionRegistry& Definitions,
	FRPGItemActionPolicyRegistry& Actions)
{
	FRPGItemDefinitionSnapshot Definition;
	Definition.DefinitionId = MakeDefinitionId();
	Definition.DefinitionVersion = 1;
	Definition.MaxStackSize = 10;
	Definitions.RegisterSnapshot(Definition);

	FRPGItemActionPolicy Action;
	Action.DefinitionId = MakeDefinitionId();
	Action.DefinitionVersion = 1;
	Action.QuantityPerUse = 2;
	Actions.RegisterPolicy(Action);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemAsyncCommandPipelineTest,
	"ProjectRPG.Item.AsyncCommand.CommitProjectionEffectAndReplay",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemAsyncCommandPipelineTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemAsyncCommandOrchestratorTests;
	const FRPGItemOwnerRef Owner = MakeOwner();
	const FGuid ItemId = FGuid::NewGuid();
	FRPGItemRecord InitialRecord;
	TestTrue(TEXT("Initial record is valid"),
		MakeRecord(Owner, ItemId, 4, 1, InitialRecord));

	FRPGInventoryProjectionStore Store;
	TArray<FRPGInventoryProjectionEntry> InitialEntries;
	TestTrue(TEXT("Projection record source initializes"),
		Store.Replace(Owner, {InitialRecord}, InitialEntries));
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	RegisterConsumePolicy(Definitions, Actions);
	FFakeCommitter Committer;
	FProjectionSink ProjectionSink(Store, Owner);
	FFirstCommitSink FirstCommitSink;
	const TSharedRef<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe> Orchestrator = MakeShared<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe>(
		Store,
		Definitions,
		Actions,
		Committer,
		ProjectionSink,
		FirstCommitSink);

	FRPGItemConsumeRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Actor = Owner;
	Request.ItemId = ItemId;
	Request.ExpectedRevision = 1;
	FRPGItemAsyncCommandOutcome FirstOutcome;
	TestTrue(TEXT("The command is submitted"),
		Orchestrator->Consume(
			Request,
			[&FirstOutcome](FRPGItemAsyncCommandOutcome Outcome)
			{
				FirstOutcome = MoveTemp(Outcome);
			}));
	TestTrue(TEXT("The asynchronous pipeline completes"),
		FirstOutcome.WasSuccessful());
	TestEqual(TEXT("Backend commit executes once"),
		Committer.CommitCount, 1);
	TestEqual(TEXT("Projection applies once"),
		ProjectionSink.ApplyCount, 1);
	TestEqual(TEXT("First-commit effect enqueues once"),
		FirstCommitSink.ApplyCount, 1);
	TestEqual(TEXT("The planner preserves the operation"),
		Committer.LastRequest.Operation, FName(TEXT("Item.Consume")));

	FRPGItemRecord StoredRecord;
	TestTrue(TEXT("Projection store retains the item"),
		Store.Find(ItemId, StoredRecord));
	TestEqual(TEXT("The authoritative quantity is updated"),
		StoredRecord.GetQuantity(), 2);
	TestEqual(TEXT("The authoritative revision is updated"),
		StoredRecord.GetRevision(), static_cast<int64>(2));

	FRPGItemAsyncCommandOutcome ReplayOutcome;
	TestTrue(TEXT("A completed client retry is handled locally"),
		Orchestrator->Consume(
			Request,
			[&ReplayOutcome](FRPGItemAsyncCommandOutcome Outcome)
			{
				ReplayOutcome = MoveTemp(Outcome);
			}));
	TestTrue(TEXT("A completed retry remains successful"),
		ReplayOutcome.WasSuccessful());
	TestTrue(TEXT("A retry is reported as already applied"),
		ReplayOutcome.BackendResult.Status ==
			ERPGItemBackendStatus::AlreadyApplied);
	TestEqual(TEXT("A retry does not call the backend twice"),
		Committer.CommitCount, 1);
	TestEqual(TEXT("A retry does not enqueue the effect twice"),
		FirstCommitSink.ApplyCount, 1);

	FRPGItemConsumeRequest ReusedId = Request;
	ReusedId.ExpectedRevision = 2;
	FRPGItemAsyncCommandOutcome ConflictOutcome;
	Orchestrator->Consume(
		ReusedId,
		[&ConflictOutcome](FRPGItemAsyncCommandOutcome Outcome)
		{
			ConflictOutcome = MoveTemp(Outcome);
		});
	TestTrue(TEXT("Request ID reuse with another fingerprint is rejected"),
		ConflictOutcome.Stage ==
			ERPGItemAsyncCommandStage::PlanRejected &&
		ConflictOutcome.PlanFailureStatus ==
			ERPGItemTransactionStatus::IdempotencyConflict);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemAsyncCommandProtocolTest,
	"ProjectRPG.Item.AsyncCommand.RejectsMismatchedReceipt",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemAsyncCommandProtocolTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemAsyncCommandOrchestratorTests;
	const FRPGItemOwnerRef Owner = MakeOwner();
	FRPGItemRecord InitialRecord;
	MakeRecord(Owner, FGuid::NewGuid(), 4, 1, InitialRecord);
	FRPGInventoryProjectionStore Store;
	TArray<FRPGInventoryProjectionEntry> Entries;
	Store.Replace(Owner, {InitialRecord}, Entries);
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	RegisterConsumePolicy(Definitions, Actions);
	FFakeCommitter Committer;
	Committer.bReturnMismatchedReceipt = true;
	FProjectionSink ProjectionSink(Store, Owner);
	FFirstCommitSink FirstCommitSink;
	const TSharedRef<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe> Orchestrator = MakeShared<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe>(
		Store,
		Definitions,
		Actions,
		Committer,
		ProjectionSink,
		FirstCommitSink);

	FRPGItemConsumeRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Actor = Owner;
	Request.ItemId = InitialRecord.GetItemId();
	Request.ExpectedRevision = 1;
	FRPGItemAsyncCommandOutcome Outcome;
	Orchestrator->Consume(
		Request,
		[&Outcome](FRPGItemAsyncCommandOutcome Result)
		{
			Outcome = MoveTemp(Result);
		});
	TestTrue(TEXT("A mismatched receipt is a protocol failure"),
		Outcome.Stage == ERPGItemAsyncCommandStage::ProtocolRejected);
	TestEqual(TEXT("A protocol failure never updates projection"),
		ProjectionSink.ApplyCount, 0);
	TestEqual(TEXT("A protocol failure never applies effects"),
		FirstCommitSink.ApplyCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeReceiptMutationTest,
	"ProjectRPG.Item.AsyncCommand.ConsumeReceiptMatchesFullPlannedMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeReceiptMutationTest::RunTest(const FString&)
{
	using namespace RPGItemAsyncCommandOrchestratorTests;
	const auto Owner = MakeOwner();
	constexpr int64 Revision = 9007199254740993LL;
	for (const auto Status : {ERPGItemBackendStatus::Succeeded, ERPGItemBackendStatus::AlreadyApplied})
	{
		for (int32 Mode = 0; Mode < 8; ++Mode)
		{
			FRPGItemRecord Initial;
			if (!TestTrue(TEXT("Consume source with exact int64 revision"), MakeRecord(Owner, FGuid::NewGuid(), 4, Revision, Initial))) return false;
			FRPGInventoryProjectionStore Store;
			TArray<FRPGInventoryProjectionEntry> Entries;
			if (!TestTrue(TEXT("Owner source loaded"), Store.Replace(Owner, {Initial}, Entries))) return false;
			FRPGItemDefinitionRegistry Definitions;
			FRPGItemActionPolicyRegistry Actions;
			RegisterConsumePolicy(Definitions, Actions);
			FFakeCommitter Committer;
			Committer.ReplyStatus = Status;
			bool bMutated = true;
			Committer.MutateReceipt = [&](FRPGItemBackendCommitResult& Result)
			{
				const auto Original = Result.Records[0];
				FRPGItemRecord Changed;
				switch (Mode)
				{
				case 0: ++Result.AffectedQuantity; break;
				case 1:
					bMutated = Original.TrySetQuantity(1, Changed);
					Result.Records[0] = Changed;
					break;
				case 2: Result.Records[0] = Original.CopyWithRevision(Revision + 2); break;
				case 3:
					bMutated = MakeRecord(Owner, FGuid::NewGuid(), 2, Revision + 1, Changed);
					Result.Records[0] = Changed;
					break;
				case 4:
					bMutated = Original.TryMoveTo({ERPGItemContainerType::Inventory, Owner.OwnerId, 5}, Changed);
					Result.Records[0] = Changed;
					break;
				case 5:
				{
					auto Metadata = Original.GetMetadata();
					Metadata.CreationSource = TEXT("UnrelatedReceiptSource");
					bMutated = FRPGItemRecord::TryRestore(Original.GetDefinitionId(), Original.GetDefinitionVersion(),
						Original.GetOwner(), Original.GetLocation(), Original.GetState(), Original.GetRevision(),
						Original.GetLifecycleState(), Metadata, Changed);
					Result.Records[0] = Changed;
					break;
				}
				case 6: Result.Records.Add(Original); break;
				case 7: Result.Records.Reset(); break;
				}
			};
			FProjectionSink ProjectionSink(Store, Owner);
			FFirstCommitSink EffectSink;
			auto Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(
				Store, Definitions, Actions, Committer, ProjectionSink, EffectSink);
			FRPGItemConsumeRequest Request;
			Request.RequestId = FGuid::NewGuid(); Request.Actor = Owner;
			Request.ItemId = Initial.GetItemId(); Request.ExpectedRevision = Revision;
			FRPGItemAsyncCommandOutcome Outcome;
			int32 Completions = 0;
			Pipeline->Consume(Request, [&](FRPGItemAsyncCommandOutcome Value) { ++Completions; Outcome = MoveTemp(Value); });
			TestTrue(TEXT("Malformed receipt fixture actually changes a valid response"), bMutated);
			TestEqual(TEXT("Exactly one completion for the malformed receipt"), Completions, 1);
			TestEqual(TEXT("A matching receipt header cannot authorize changed item contents"), Outcome.Stage, ERPGItemAsyncCommandStage::ProtocolRejected);
			TestEqual(TEXT("Changed consume receipt never reaches the projection sink"), ProjectionSink.ApplyCount, 0);
			TestEqual(TEXT("Changed consume receipt never reaches the gameplay effect sink"), EffectSink.ApplyCount, 0);
			FRPGItemRecord Stored;
			if (!TestTrue(TEXT("Original item remains in the source"), Store.Find(Initial.GetItemId(), Stored))) return false;
			TestTrue(TEXT("A rejected receipt leaves every original item field unchanged"),
				FRPGItemRecord::StaticStruct()->CompareScriptStruct(&Initial, &Stored, 0));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeReceiptPositiveTest,
	"ProjectRPG.Item.AsyncCommand.ConsumeFinalStackAndAlreadyAppliedStayValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeReceiptPositiveTest::RunTest(const FString&)
{
	using namespace RPGItemAsyncCommandOrchestratorTests;
	const auto Owner = MakeOwner();
	constexpr int64 Revision = 9007199254740993LL;
	for (const int32 Quantity : {2, 4})
	{
		for (const auto Status : {ERPGItemBackendStatus::Succeeded, ERPGItemBackendStatus::AlreadyApplied})
		{
			FRPGItemRecord Initial;
			if (!MakeRecord(Owner, FGuid::NewGuid(), Quantity, Revision, Initial)) return false;
			FRPGInventoryProjectionStore Store;
			TArray<FRPGInventoryProjectionEntry> Entries;
			if (!Store.Replace(Owner, {Initial}, Entries)) return false;
			FRPGItemDefinitionRegistry Definitions;
			FRPGItemActionPolicyRegistry Actions;
			RegisterConsumePolicy(Definitions, Actions);
			FFakeCommitter Committer; Committer.ReplyStatus = Status;
			FProjectionSink ProjectionSink(Store, Owner);
			FFirstCommitSink EffectSink;
			auto Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(
				Store, Definitions, Actions, Committer, ProjectionSink, EffectSink);
			FRPGItemConsumeRequest Request;
			Request.RequestId = FGuid::NewGuid(); Request.Actor = Owner;
			Request.ItemId = Initial.GetItemId(); Request.ExpectedRevision = Revision;
			FRPGItemAsyncCommandOutcome Outcome;
			Pipeline->Consume(Request, [&](FRPGItemAsyncCommandOutcome Value) { Outcome = MoveTemp(Value); });
			TestTrue(TEXT("An exact consume receipt remains valid"), Outcome.WasSuccessful());
			TestEqual(TEXT("Exact receipt updates projection once"), ProjectionSink.ApplyCount, 1);
			TestEqual(TEXT("AlreadyApplied cannot replay the one-time effect"), EffectSink.ApplyCount,
				Status == ERPGItemBackendStatus::Succeeded ? 1 : 0);
			FRPGItemRecord Stored;
			if (!TestTrue(TEXT("Active or terminal identity remains queryable"), Store.Find(Initial.GetItemId(), Stored))) return false;
			TestEqual(TEXT("Revision above double precision advances exactly once"), Stored.GetRevision(), Revision + 1);
			TestEqual(TEXT("Only the planned quantity is consumed"), Stored.GetQuantity(), Quantity - 2);
			TestEqual(TEXT("Final stack preserves its terminal lifecycle"), Stored.GetLifecycleState(),
				Quantity == 2 ? ERPGItemLifecycleState::Consumed : ERPGItemLifecycleState::Active);
		}
	}
	return true;
}

#endif
