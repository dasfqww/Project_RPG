#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Component/RPGItemCommandComponent.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "Item/Projection/RPGInventoryProjectionStore.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "RPGItemTags.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

namespace RPGItemMailTests
{
FRPGItemOwnerRef Owner()
{
	FRPGItemOwnerRef Result;
	Result.Type = ERPGItemOwnerType::Character;
	Result.OwnerId = TEXT("12345678-1234-1234-1234-123456789abc");
	return Result;
}

FPrimaryAssetId DefinitionId()
{
	return FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("MailMaterial"));
}

FRPGItemRecord MakeRecord(ERPGItemContainerType Type = ERPGItemContainerType::Mail,
	FString Container = TEXT("DungeonReward.first"), int32 Slot = 0,
	FRPGItemRecordMetadata Metadata = {})
{
	FRPGItemInstanceState State;
	FGameplayTagContainer Tags;
	Tags.AddTag(RPGGameplayTags::GameItem_Craft_fruit);
	TArray<FRPGItemStatValue> Stats;
	Stats.Add({ RPGGameplayTags::Fragment_StatMod_1, 7.5f });
	FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 104729, 3, Tags, Stats, State);
	FRPGItemLocation Location;
	Location.ContainerType = Type;
	Location.ContainerId = MoveTemp(Container);
	Location.SlotIndex = Slot;
	Metadata.CreationSource = TEXT("DungeonClear.mail_test_v1");
	FRPGItemRecord Result;
	FRPGItemRecord::TryRestore(DefinitionId(), 1, Owner(), Location, State, 1,
		ERPGItemLifecycleState::Active, Metadata, Result);
	return Result;
}

void RegisterDefinition(FRPGItemDefinitionRegistry& Definitions, int32 Version = 1, int32 MaxStack = 10)
{
	FRPGItemDefinitionSnapshot Snapshot;
	Snapshot.DefinitionId = DefinitionId();
	Snapshot.DefinitionVersion = Version;
	Snapshot.MaxStackSize = MaxStack;
	Definitions.RegisterSnapshot(Snapshot);
}

FRPGItemClaimMailRequest MakeRequest(const FRPGItemRecord& Record, int32 Slot = 0)
{
	FRPGItemClaimMailRequest Result;
	Result.RequestId = FGuid::NewGuid();
	Result.Actor = Owner();
	Result.ItemId = Record.GetItemId();
	Result.ExpectedRevision = Record.GetRevision();
	Result.InventoryDestination.ContainerType = ERPGItemContainerType::Inventory;
	Result.InventoryDestination.ContainerId = Result.Actor.OwnerId;
	Result.InventoryDestination.SlotIndex = Slot;
	return Result;
}

struct FCommitter final : IRPGItemAsyncCommitter
{
	int32 Count = 0;
	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) override
	{
		++Count;
		FRPGItemBackendCommitResult Result;
		Result.Status = ERPGItemBackendStatus::Succeeded;
		Result.RequestId = Request.RequestId;
		Result.Operation = Request.Operation;
		Result.CommandFingerprint = Request.CommandFingerprint;
		Result.Actor = Request.Actor;
		Result.AffectedQuantity = Request.AffectedQuantity;
		for (const auto& Mutation : Request.Mutations)
		{
			Result.Records.Add(Mutation.NewRecord.CopyWithRevision(Mutation.ExpectedRevision + 1));
		}
		Completion(MoveTemp(Result));
		return true;
	}
};

struct FSink final : IRPGItemCommitSink, IRPGItemFirstCommitSink
{
	explicit FSink(FRPGInventoryProjectionStore& InStore) : Store(InStore) {}
	FRPGInventoryProjectionStore& Store;
	int32 FirstCount = 0;
	virtual bool ApplyCommit(const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& Result, FString& OutError) override
	{
		TArray<FRPGInventoryProjectionEntry> Entries;
		return Store.ApplyMutations(ExpectedOwner, Result.Records, Entries, &OutError);
	}
	virtual bool EnqueueFirstCommit(const FRPGItemBackendCommitResult&, FString&) override
	{
		++FirstCount;
		return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMailClaimValidationTest,
	"ProjectRPG.Item.Mail.ClaimValidationAndPreservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMailClaimValidationTest::RunTest(const FString&)
{
	using namespace RPGItemMailTests;
	FRPGInventoryProjectionStore Store;
	TArray<FRPGInventoryProjectionEntry> Entries;
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions; // Materials need no action policy to claim.
	RegisterDefinition(Definitions);
	FRPGItemActionCommandPlanner Planner(Store, Definitions, Actions);
	FRPGItemRecordMetadata Metadata;
	Metadata.BindState = ERPGItemBindState::CharacterBound;
	Metadata.Durability.Maximum = 100;
	Metadata.Durability.Current = 42;
	Metadata.ExpiresAtUtc = FDateTime::UtcNow() + FTimespan::FromDays(1);
	const FRPGItemRecord Mail = MakeRecord(ERPGItemContainerType::Mail, TEXT("DungeonReward.first"), 0, Metadata);
	TestTrue(TEXT("Mail fixture is valid"), Mail.IsStructurallyValid());
	TestTrue(TEXT("Mail load"), Store.Replace(Owner(), { Mail }, Entries));
	const FRPGItemClaimMailRequest Request = MakeRequest(Mail);
	const FRPGItemCommandPlan Plan = Planner.Prepare(Request);
	TestTrue(TEXT("Claim prepared without consume/equip policy"), Plan.IsReady());
	if (!Plan.IsReady()) return false;
	const FRPGItemRecord& Claimed = Plan.CommitRequest.Mutations[0].NewRecord;
	TestEqual(TEXT("Same identity"), Claimed.GetItemId(), Mail.GetItemId());
	TestEqual(TEXT("Full stack moved"), Claimed.GetQuantity(), 3);
	TestEqual(TEXT("Affected quantity"), Plan.CommitRequest.AffectedQuantity, 3);
	TestEqual(TEXT("Operation"), Plan.CommitRequest.Operation, FName(TEXT("Item.ClaimMail")));
	TestEqual(TEXT("Seed preserved"), Claimed.GetState().GetGenerationSeed(), 104729);
	TestEqual(TEXT("Stat preserved"), Claimed.GetState().GetStatValue(RPGGameplayTags::Fragment_StatMod_1), 7.5f);
	TestTrue(TEXT("Tags preserved"), Claimed.GetState().HasInstanceTag(RPGGameplayTags::GameItem_Craft_fruit));
	TestEqual(TEXT("Audit source preserved"), Claimed.GetMetadata().CreationSource, Mail.GetMetadata().CreationSource);
	TestEqual(TEXT("Binding preserved"), Claimed.GetMetadata().BindState, Metadata.BindState);
	TestEqual(TEXT("Durability preserved"), Claimed.GetMetadata().Durability.Current, Metadata.Durability.Current);
	TestEqual(TEXT("Expiration preserved"), Claimed.GetMetadata().ExpiresAtUtc, Metadata.ExpiresAtUtc);
	TestEqual(TEXT("Location changed"), Claimed.GetLocation().ContainerType, ERPGItemContainerType::Inventory);
	TestEqual(TEXT("CAS expected revision"), Plan.CommitRequest.Mutations[0].ExpectedRevision, int64(1));
	auto Invalid = Request;
	Invalid.InventoryDestination.SlotIndex = -1;
	TestEqual(TEXT("Negative slot"), Planner.Prepare(Invalid).FailureStatus, ERPGItemTransactionStatus::InvalidRequest);
	Invalid.InventoryDestination.SlotIndex = RPGItemInventoryPolicy::SlotCapacity;
	TestEqual(TEXT("Capacity bounded"), Planner.Prepare(Invalid).FailureStatus, ERPGItemTransactionStatus::InvalidRequest);
	Invalid = Request;
	Invalid.InventoryDestination.ContainerId = TEXT("other-character");
	TestEqual(TEXT("Cannot choose another bag"), Planner.Prepare(Invalid).FailureStatus, ERPGItemTransactionStatus::InvalidRequest);
	Invalid = Request;
	Invalid.Actor.OwnerId = TEXT("other-character");
	Invalid.InventoryDestination.ContainerId = Invalid.Actor.OwnerId;
	TestEqual(TEXT("Cannot claim another owner"), Planner.Prepare(Invalid).FailureStatus, ERPGItemTransactionStatus::Forbidden);
	Invalid = Request;
	Invalid.ExpectedRevision = 2;
	TestEqual(TEXT("Stale revision"), Planner.Prepare(Invalid).FailureStatus, ERPGItemTransactionStatus::RevisionConflict);
	Invalid = Request;
	Invalid.ItemId = FGuid::NewGuid();
	TestEqual(TEXT("Unknown item"), Planner.Prepare(Invalid).FailureStatus, ERPGItemTransactionStatus::NotFound);
	const auto Occupied = MakeRecord(ERPGItemContainerType::Inventory, Owner().OwnerId);
	Store.Replace(Owner(), { Mail, Occupied }, Entries);
	TestEqual(TEXT("Occupied bag slot"), Planner.Prepare(Request).FailureStatus, ERPGItemTransactionStatus::LocationOccupied);
	FRPGItemRecordMetadata Locked;
	Locked.bLocked = true;
	const auto LockedMail = MakeRecord(ERPGItemContainerType::Mail, TEXT("Locked"), 0, Locked);
	Store.Replace(Owner(), { LockedMail }, Entries);
	TestEqual(TEXT("Locked attachment"), Planner.Prepare(MakeRequest(LockedMail)).FailureStatus, ERPGItemTransactionStatus::Locked);
	FRPGItemRecordMetadata Expired;
	Expired.ExpiresAtUtc = FDateTime::UtcNow() - FTimespan::FromDays(1);
	const auto ExpiredMail = MakeRecord(ERPGItemContainerType::Mail, TEXT("Expired"), 0, Expired);
	Store.Replace(Owner(), { ExpiredMail }, Entries);
	TestEqual(TEXT("Expired attachment"), Planner.Prepare(MakeRequest(ExpiredMail)).FailureStatus, ERPGItemTransactionStatus::Expired);
	FRPGItemRecord ConsumedMail;
	Mail.TryMarkConsumed(ConsumedMail);
	Store.Replace(Owner(), { ConsumedMail }, Entries);
	TestEqual(TEXT("Terminal item cannot be claimed"), Planner.Prepare(Request).FailureStatus, ERPGItemTransactionStatus::NotActive);
	Store.Replace(Owner(), { Occupied }, Entries);
	TestEqual(TEXT("Inventory cannot be claimed again"), Planner.Prepare(MakeRequest(Occupied, 1)).FailureStatus,
		ERPGItemTransactionStatus::InvalidContainerTransition);
	Store.Replace(Owner(), { Mail }, Entries);
	Definitions.Reset();
	TestEqual(TEXT("Missing definition"), Planner.Prepare(Request).FailureStatus, ERPGItemTransactionStatus::DefinitionUnavailable);
	RegisterDefinition(Definitions, 2);
	TestEqual(TEXT("Definition version mismatch"), Planner.Prepare(Request).FailureStatus, ERPGItemTransactionStatus::DefinitionVersionMismatch);
	Definitions.Reset();
	RegisterDefinition(Definitions, 1, 2);
	TestEqual(TEXT("Oversized stack cannot enter bag"), Planner.Prepare(Request).FailureStatus, ERPGItemTransactionStatus::StackFull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMailProjectionTest,
	"ProjectRPG.Item.Mail.SeparateProjectionAndAtomicValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMailProjectionTest::RunTest(const FString&)
{
	using namespace RPGItemMailTests;
	const auto First = MakeRecord();
	const auto Second = MakeRecord(ERPGItemContainerType::Mail, TEXT("DungeonReward.second"));
	TArray<FRPGInventoryProjectionEntry> Inventory, Mail;
	FRPGInventoryProjectionStore Store;
	TestTrue(TEXT("Different mails may reuse slot zero"), Store.Replace(Owner(), { First, Second }, Inventory));
	TestTrue(TEXT("Mail cannot appear as usable inventory"), Inventory.IsEmpty());
	TestTrue(TEXT("Build mail projection"), FRPGInventoryProjectionMapper::BuildMailSnapshot(Owner(), { First, Second }, Mail));
	TestEqual(TEXT("Both attachments visible"), Mail.Num(), 2);
	const auto Collision = MakeRecord();
	TestFalse(TEXT("Duplicate full mail location rejected"), Store.Replace(Owner(), { First, Collision }, Inventory));
	TestEqual(TEXT("Previous store preserved"), Store.NumAuthoritativeRecords(), 2);
	FRPGItemRecord Found;
	TestTrue(TEXT("Previous second item preserved"), Store.Find(Second.GetItemId(), Found));
	TestFalse(TEXT("Invalid mutation rejected atomically"), Store.ApplyMutations(Owner(), { Collision }, Inventory));
	TestEqual(TEXT("Store unchanged after mutation rejection"), Store.NumAuthoritativeRecords(), 2);
	FRPGItemRecord Claimed;
	First.TryMoveTo(MakeRequest(First).InventoryDestination, Claimed);
	Claimed = Claimed.CopyWithRevision(2);
	TestTrue(TEXT("Claim updates store"), Store.ApplyMutations(Owner(), { Claimed }, Inventory));
	TArray<FRPGItemRecord> Records;
	Store.FindByOwner(Owner(), Records);
	FRPGInventoryProjectionMapper::BuildMailSnapshot(Owner(), Records, Mail);
	TestEqual(TEXT("One unclaimed attachment remains"), Mail.Num(), 1);
	TestEqual(TEXT("Exactly one inventory item"), Inventory.Num(), 1);
	TestEqual(TEXT("Same ID in inventory"), Inventory[0].GetItemId(), First.GetItemId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMailReplayTest,
	"ProjectRPG.Item.Mail.AsyncClaimReplayDoesNotDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMailReplayTest::RunTest(const FString&)
{
	using namespace RPGItemMailTests;
	FRPGInventoryProjectionStore Store;
	TArray<FRPGInventoryProjectionEntry> Entries;
	const auto Mail = MakeRecord();
	Store.Replace(Owner(), { Mail }, Entries);
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	RegisterDefinition(Definitions);
	FCommitter Committer;
	FSink Sink(Store);
	const auto Orchestrator = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(
		Store, Definitions, Actions, Committer, Sink, Sink);
	const auto Request = MakeRequest(Mail);
	FRPGItemAsyncCommandOutcome Outcome;
	auto Capture = [&Outcome](FRPGItemAsyncCommandOutcome Result) { Outcome = MoveTemp(Result); };
	TestTrue(TEXT("Claim submitted"), Orchestrator->ClaimMail(Request, Capture));
	TestTrue(TEXT("Claim completed"), Outcome.WasSuccessful());
	TestTrue(TEXT("Original request replay succeeds after item moved"), Orchestrator->ClaimMail(Request, Capture));
	TestEqual(TEXT("Idempotent receipt"), Outcome.BackendResult.Status, ERPGItemBackendStatus::AlreadyApplied);
	TestEqual(TEXT("Only one DB commit"), Committer.Count, 1);
	TestEqual(TEXT("Only one first-commit notification"), Sink.FirstCount, 1);
	auto Changed = Request;
	Changed.InventoryDestination.SlotIndex = 1;
	Orchestrator->ClaimMail(Changed, Capture);
	TestEqual(TEXT("Changed replay fingerprint rejected"), Outcome.PlanFailureStatus, ERPGItemTransactionStatus::IdempotencyConflict);
	Changed = Request;
	Changed.RequestId = FGuid::NewGuid();
	Changed.ExpectedRevision = 2;
	TestFalse(TEXT("Fresh request cannot re-claim inventory item"), Orchestrator->ClaimMail(Changed, Capture));
	TestEqual(TEXT("Wrong source container"), Outcome.PlanFailureStatus, ERPGItemTransactionStatus::InvalidContainerTransition);
	FRPGItemRecord Found;
	Store.Find(Mail.GetItemId(), Found);
	TestEqual(TEXT("Revision incremented once"), Found.GetRevision(), int64(2));
	TestEqual(TEXT("Quantity unchanged"), Found.GetQuantity(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardMailReservationLifecycleTest,
	"ProjectRPG.Item.Mail.ReservationSurvivesSplitProjectionAndReleasesOnFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardMailReservationLifecycleTest::RunTest(const FString&)
{
	using namespace RPGItemMailTests;
	const FRPGItemRecord Mail = MakeRecord();
	const FRPGItemClaimMailRequest Claim = MakeRequest(Mail);
	FRPGItemRecord Claimed;
	TestTrue(TEXT("Claimed record can be created"),
		Mail.TryMoveTo(Claim.InventoryDestination, Claimed));
	Claimed = Claimed.CopyWithRevision(2);
	TArray<FRPGInventoryProjectionEntry> MailSnapshot, InventorySnapshot;
	TestTrue(TEXT("Mail snapshot"),
		FRPGInventoryProjectionMapper::BuildMailSnapshot(Owner(), { Mail }, MailSnapshot));
	TestTrue(TEXT("Inventory snapshot"),
		FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner(), { Claimed }, InventorySnapshot));
	if (MailSnapshot.Num() != 1 || InventorySnapshot.Num() != 1) return false;

	URPGInventoryProjectionViewModel* ViewModel = NewObject<URPGInventoryProjectionViewModel>();
	ViewModel->MailItems = MailSnapshot;
	ViewModel->PendingMailClaimsByRequest.Add(Claim.RequestId, Mail.GetItemId());
	ViewModel->PendingMailItemIds.Add(Mail.GetItemId());
	ViewModel->PendingMailSlotsByItem.Add(Mail.GetItemId(), 0);
	TestTrue(TEXT("Mail claim reserves slot zero"), ViewModel->IsInventorySlotUnavailable(0));
	FRPGItemCommandClientResult Unknown;
	Unknown.RequestId = Claim.RequestId;
	Unknown.Operation = TEXT("Item.ClaimMail");
	Unknown.Result = ERPGItemCommandResultCode::Uncertain;
	ViewModel->HandleCommandCompleted(Unknown);
	TestTrue(TEXT("Unknown result retains the attachment and destination reservation"),
		ViewModel->IsMailClaimPending(Mail.GetItemId()) && ViewModel->IsInventorySlotUnavailable(0));
	TestTrue(TEXT("Unknown result retains the original request correlation"),
		ViewModel->PendingMailClaimsByRequest.Contains(Claim.RequestId));
	TestEqual(TEXT("Unknown claim remains part of the shared pending count"), ViewModel->PendingItemCommandCount, 1);
	URPGItemCommandComponent* Commands = NewObject<URPGItemCommandComponent>();
	ViewModel->CommandComponent = Commands;
	FRPGInventoryCommandRecovery Recovery;
	Recovery.RequestId = Claim.RequestId; Recovery.Operation = TEXT("Item.ClaimMail");
	Commands->InventoryRecovery.Add(Recovery);
	Unknown.Result = ERPGItemCommandResultCode::InvalidRequest;
	ViewModel->HandleCommandCompleted(Unknown);
	TestTrue(TEXT("Rejecting a changed replay cannot unlock the original frozen claim"),
		ViewModel->IsMailClaimPending(Mail.GetItemId()) && ViewModel->IsInventorySlotUnavailable(0) &&
		ViewModel->PendingMailClaimsByRequest.Contains(Claim.RequestId));
	Commands->InventoryRecovery.Reset();
	Unknown.RequestId = FGuid::NewGuid();
	Unknown.Result = ERPGItemCommandResultCode::Conflict;
	ViewModel->HandleCommandCompleted(Unknown);
	TestTrue(TEXT("An unrelated rejection cannot release the original claim"),
		ViewModel->IsMailClaimPending(Mail.GetItemId()));

	// The owner-only mail deletion can arrive before the inventory addition.
	ViewModel->MailItems.Reset();
	ViewModel->ReconcilePendingMailClaims();
	TestTrue(TEXT("Mail-only delta must not free the reservation"),
		ViewModel->IsMailClaimPending(Mail.GetItemId()));
	FRPGItemCommandClientResult Success;
	Success.RequestId = Claim.RequestId;
	Success.Operation = TEXT("Item.ClaimMail");
	Success.Result = ERPGItemCommandResultCode::Succeeded;
	ViewModel->HandleCommandCompleted(Success);
	TestTrue(TEXT("Success response cannot free a split projection"),
		ViewModel->IsInventorySlotUnavailable(0));
	TestTrue(TEXT("Success keeps correlation until both projections settle"),
		ViewModel->PendingMailClaimsByRequest.Contains(Claim.RequestId));
	ViewModel->InventoryItems = InventorySnapshot;
	ViewModel->ReconcilePendingMailClaims();
	TestFalse(TEXT("Settled projection releases pending claim"),
		ViewModel->IsMailClaimPending(Mail.GetItemId()));
	TestFalse(TEXT("Settled projection also retires the original request correlation"),
		ViewModel->PendingMailClaimsByRequest.Contains(Claim.RequestId));
	TestTrue(TEXT("Committed inventory item still occupies slot zero"),
		ViewModel->IsInventorySlotUnavailable(0));
	URPGInventoryProjectionViewModel* InventoryFirst = NewObject<URPGInventoryProjectionViewModel>();
	InventoryFirst->MailItems = MailSnapshot;
	InventoryFirst->InventoryItems = InventorySnapshot;
	InventoryFirst->PendingMailClaimsByRequest.Add(Claim.RequestId, Mail.GetItemId());
	InventoryFirst->PendingMailItemIds.Add(Mail.GetItemId());
	InventoryFirst->PendingMailSlotsByItem.Add(Mail.GetItemId(), 0);
	Unknown.RequestId = Claim.RequestId;
	Unknown.Result = ERPGItemCommandResultCode::Uncertain;
	InventoryFirst->HandleCommandCompleted(Unknown);
	TestTrue(TEXT("Inventory-first delta alone also keeps an uncertain claim reserved"),
		InventoryFirst->IsMailClaimPending(Mail.GetItemId()));
	InventoryFirst->MailItems.Reset();
	InventoryFirst->ReconcilePendingMailClaims();
	TestTrue(TEXT("Both authoritative projections retire an uncertain claim without an ACK"),
		!InventoryFirst->IsMailClaimPending(Mail.GetItemId()) && InventoryFirst->PendingMailClaimsByRequest.IsEmpty());

	URPGInventoryProjectionViewModel* RejectedViewModel = NewObject<URPGInventoryProjectionViewModel>();
	const FGuid RejectedRequestId = FGuid::NewGuid();
	RejectedViewModel->MailItems = MailSnapshot;
	RejectedViewModel->PendingMailClaimsByRequest.Add(RejectedRequestId, Mail.GetItemId());
	RejectedViewModel->PendingMailItemIds.Add(Mail.GetItemId());
	RejectedViewModel->PendingMailSlotsByItem.Add(Mail.GetItemId(), 0);
	FRPGItemCommandClientResult Rejected;
	Rejected.RequestId = RejectedRequestId;
	Rejected.Operation = TEXT("Item.ClaimMail");
	Rejected.Result = ERPGItemCommandResultCode::Conflict;
	RejectedViewModel->HandleCommandCompleted(Rejected);
	TestFalse(TEXT("Definitive rejection releases pending claim"),
		RejectedViewModel->IsMailClaimPending(Mail.GetItemId()));
	TestFalse(TEXT("Definitive rejection frees its reserved slot"),
		RejectedViewModel->IsInventorySlotUnavailable(0));
	return true;
}

#endif
