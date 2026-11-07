#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Persistence/RPGInMemoryItemRepository.h"
#include "Item/Transaction/RPGInventorySortPlanner.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Transaction/RPGInventoryCommandJournal.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemActionPolicyTypes.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Transaction/RPGItemPickupGrantPlanner.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace InventoryTabsTests
{
const FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, TEXT("11111111-1111-4111-8111-111111111111")};
FRPGItemRecord Make(const TCHAR* Name, int32 Slot, int32 Quantity = 1, bool bLocked = false)
{
	FRPGItemInstanceState State;
	FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 17, Quantity, {}, {}, State);
	FRPGItemRecordMetadata Metadata;
	Metadata.bLocked = bLocked;
	FRPGItemRecord Item;
	FRPGItemRecord::TryCreate(FPrimaryAssetId(TEXT("RPGItemDefinition"), FName(Name)), 1, Owner,
		{ERPGItemContainerType::Inventory, Owner.OwnerId, Slot}, State, Metadata, Item);
	return Item;
}
bool Seed(FRPGInMemoryItemRepository& Repo, const TArray<FRPGItemRecord>& Items)
{
	FRPGItemRepositoryCommitRequest Request;
	Request.RequestId = FGuid::NewGuid(); Request.Operation = TEXT("Test.Seed"); Request.CommandFingerprint = TEXT("seed"); Request.Actor = Owner;
	for (const auto& Item : Items) Request.Mutations.Add({0, Item});
	return Repo.Commit(Request).WasApplied();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryTabsLayoutTest, "ProjectRPG.Item.InventoryTabs.Layout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryTabsLayoutTest::RunTest(const FString&)
{
	using namespace RPGItemInventoryPolicy;
	TestEqual(TEXT("Three independent bags"), SlotCapacity, 240);
	for (const auto Category : {EItemCategory::Equip, EItemCategory::Consume, EItemCategory::Craft})
	{
		int32 Count = 0;
		for (int32 Slot = -1; Slot <= SlotCapacity; ++Slot) Count += Contains(Category, Slot) ? 1 : 0;
		TestEqual(TEXT("Each bag has exactly 80 positions"), Count, 80);
	}
	TestFalse(TEXT("Unknown category is not a tab"), IsTab(EItemCategory::None));
	TestEqual(TEXT("Equipment namespace"), CategoryForDefinition({TEXT("RPGItemDefinition"), TEXT("GameItem.Equipment.Weapon.A")}), EItemCategory::Equip);
	TestEqual(TEXT("Consumable namespace"), CategoryForDefinition({TEXT("RPGItemDefinition"), TEXT("GameItem.Consume.Potion.A")}), EItemCategory::Consume);
	TestEqual(TEXT("Other namespace"), CategoryForDefinition({TEXT("RPGItemDefinition"), TEXT("GameItem.Material.Ore")}), EItemCategory::Craft);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryTabsSortTest, "ProjectRPG.Item.InventoryTabs.StableSort", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryTabsSortTest::RunTest(const FString&)
{
	using namespace InventoryTabsTests;
	FRPGInMemoryItemRepository Repo;
	const auto A = Make(TEXT("GameItem.Consume.Potion.A"), 88, 3);
	const auto Equal = Make(TEXT("GameItem.Consume.Potion.A"), 92, 3);
	const auto Large = Make(TEXT("GameItem.Consume.Potion.A"), 95, 9);
	const auto Locked = Make(TEXT("GameItem.Consume.Potion.Z"), 81, 2, true);
	const auto Equipment = Make(TEXT("GameItem.Equipment.Weapon.A"), 4);
	TestTrue(TEXT("Fixture commits"), Seed(Repo, {Equal, Locked, Equipment, Large, A}));
	auto Plan = FRPGInventorySortPlanner::Prepare(Repo, FGuid::NewGuid(), Owner, EItemCategory::Consume);
	TestTrue(TEXT("Server plan ready"), Plan.IsReady());
	if (!Plan.IsReady()) return false;
	TestEqual(TEXT("Only this tab's mutable items"), Plan.CommitRequest.Mutations.Num(), 3);
	TestEqual(TEXT("Larger stack first"), Plan.CommitRequest.Mutations[0].NewRecord.GetItemId(), Large.GetItemId());
	TestEqual(TEXT("Equal keys retain old slot order A"), Plan.CommitRequest.Mutations[1].NewRecord.GetItemId(), A.GetItemId());
	TestEqual(TEXT("Equal keys retain old slot order B"), Plan.CommitRequest.Mutations[2].NewRecord.GetItemId(), Equal.GetItemId());
	TestEqual(TEXT("Locked slot skipped"), Plan.CommitRequest.Mutations[1].NewRecord.GetLocation().SlotIndex, 82);
	TestTrue(TEXT("Atomic sorted commit"), Repo.Commit(Plan.CommitRequest).WasApplied());
	FRPGItemRecord After;
	Repo.Find(Equipment.GetItemId(), After);
	TestEqual(TEXT("Other tab untouched"), After.GetLocation().SlotIndex, 4);
	Repo.Find(Locked.GetItemId(), After);
	TestEqual(TEXT("Locked position unchanged"), After.GetLocation().SlotIndex, 81);
	const auto Noop = FRPGInventorySortPlanner::Prepare(Repo, FGuid::NewGuid(), Owner, EItemCategory::Consume);
	TestFalse(TEXT("Repeated sort sends no commit"), Noop.IsReady());
	TestEqual(TEXT("Explicit no-change"), Noop.FailureStatus, ERPGItemTransactionStatus::NoChange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryTabsFullSortTest, "ProjectRPG.Item.InventoryTabs.FullSortRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryTabsFullSortTest::RunTest(const FString&)
{
	using namespace InventoryTabsTests;
	FRPGInMemoryItemRepository Repo;
	TArray<FRPGItemRecord> Items;
	for (int32 Slot = 0; Slot < 80; ++Slot) Items.Add(Make(*FString::Printf(TEXT("GameItem.Equipment.Weapon.%03d"), 79 - Slot), Slot));
	TestTrue(TEXT("Seed full tab"), Seed(Repo, Items));
	auto Plan = FRPGInventorySortPlanner::Prepare(Repo, FGuid::NewGuid(), Owner, EItemCategory::Equip);
	TestTrue(TEXT("Full permutation needs no empty slot"), Plan.IsReady());
	TestEqual(TEXT("80 CAS mutations"), Plan.CommitRequest.Mutations.Num(), 80);
	FString Body;
	TestTrue(TEXT("Full sort wire encoding"), FRPGItemBackendJsonCodec::SerializeCommitRequest(Plan.CommitRequest, Body));
	FRPGItemRepositoryCommitRequest Restored;
	TestTrue(TEXT("Full sort wire decoding"), FRPGItemBackendJsonCodec::DeserializeCommitRequest(Body, Restored));
	const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("InventoryTabsJournal"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FRPGInventoryJournalScope Scope{TEXT("http://127.0.0.1:9999"), TEXT("test"), TEXT("22222222-2222-4222-8222-222222222222"), Owner.OwnerId, TEXT("76561198000000001")};
	{
		FRPGInventoryCommandJournal Journal(Root, Scope);
		TestTrue(TEXT("Journal initializes"), Journal.Restore());
		TestTrue(TEXT("Frozen full plan persisted"), Journal.Persist(Plan.CommitRequest));
		FRPGInventoryCommandJournal Reloaded(Root, Scope);
		TestTrue(TEXT("Journal reload"), Reloaded.Restore());
		TestEqual(TEXT("Full plan survives restart"), Reloaded.GetPending().Num(), 1);
		TestTrue(TEXT("Full cyclic transaction"), Repo.Commit(Restored).WasApplied());
		TestTrue(TEXT("Idempotent replay"), Repo.Commit(Restored).Status == ERPGItemRepositoryCommitStatus::AlreadyCommitted);
		TestTrue(TEXT("Retire verified plan"), Reloaded.ForgetVerified(Restored));
	}
	// This is an owned fresh child of the test runtime's Saved directory, with no links.
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}
namespace InventoryTabsTests
{
class FUnusedCatalog final : public IRPGItemDefinitionCatalog, public IRPGItemActionPolicyCatalog
{
public:
	virtual bool TryFindDefinition(const FPrimaryAssetId&, FRPGItemDefinitionSnapshot&) const override { return false; }
	virtual bool TryFindActionPolicy(const FPrimaryAssetId&, FRPGItemActionPolicy&) const override { return false; }
};
class FLostResponseCommitter final : public IRPGItemAsyncCommitter
{
public:
	FRPGInMemoryItemRepository& Repo;
	FString FirstBody;
	bool bSameRetry = false;
	int32 Calls = 0;
	explicit FLostResponseCommitter(FRPGInMemoryItemRepository& InRepo) : Repo(InRepo) {}
	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion) override
	{
		FString Body;
		FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Body);
		const auto Receipt = Repo.Commit(Request);
		FRPGItemBackendCommitResult Result;
		Result.RequestId = Request.RequestId; Result.Operation = Request.Operation;
		Result.CommandFingerprint = Request.CommandFingerprint; Result.Actor = Request.Actor;
		Result.AffectedQuantity = Request.AffectedQuantity;
		if (++Calls == 1)
		{ FirstBody = Body; Result.Status = ERPGItemBackendStatus::TransportError; Result.bCommitOutcomeUncertain = true; }
		else
		{ bSameRetry = Body == FirstBody; Result.Status = ERPGItemBackendStatus::AlreadyApplied; Result.Records = Receipt.Records; }
		Completion(MoveTemp(Result));
		return true;
	}
};
class FSortSink final : public IRPGItemCommitSink, public IRPGItemFirstCommitSink
{
public:
	int32 Applies = 0, Effects = 0;
	virtual bool ApplyCommit(const FRPGItemOwnerRef&, const FRPGItemBackendCommitResult&, FString&) override { ++Applies; return true; }
	virtual bool EnqueueFirstCommit(const FRPGItemBackendCommitResult&, FString&) override { ++Effects; return true; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryTabsLostResponseTest, "ProjectRPG.Item.InventoryTabs.LostResponse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryTabsLostResponseTest::RunTest(const FString&)
{
	using namespace InventoryTabsTests;
	FRPGInMemoryItemRepository Repo;
	TestTrue(TEXT("Fixture"), Seed(Repo, {Make(TEXT("GameItem.Material.Z"), 160), Make(TEXT("GameItem.Material.A"), 161)}));
	FUnusedCatalog Catalog;
	FSortSink Sink;
	FLostResponseCommitter Committer(Repo);
	auto Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(Repo, Catalog, Catalog, Committer, Sink, Sink);
	const FGuid Request = FGuid::NewGuid();
	FRPGItemAsyncCommandOutcome Outcome;
	Pipeline->Sort(Request, Owner, EItemCategory::Craft, [&Outcome](auto Value) { Outcome = MoveTemp(Value); });
	TestEqual(TEXT("Lost response stays uncertain"), Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
	TestTrue(TEXT("Frozen plan retained"), Pipeline->HasUncertainInventoryCommands());
	Pipeline->RetryInventoryCommand(Request, Owner, [&Outcome](auto Value) { Outcome = MoveTemp(Value); });
	TestTrue(TEXT("Original receipt recovers the command"), Outcome.WasSuccessful());
	TestTrue(TEXT("Retry reuses exact original bytes"), Committer.bSameRetry);
	TestFalse(TEXT("Recovery retired"), Pipeline->HasUncertainInventoryCommands());
	TestEqual(TEXT("Apply once after confirmed receipt"), Sink.Applies, 1);
	TestEqual(TEXT("Replay has no first-commit effects"), Sink.Effects, 0);
	TArray<FRPGItemRecord> Items;
	Repo.FindByOwner(Owner, Items);
	for (const auto& Item : Items) TestEqual(TEXT("No duplicate revision increment"), Item.GetRevision(), int64(2));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryTabsPickupTest, "ProjectRPG.Item.InventoryTabs.PickupCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryTabsPickupTest::RunTest(const FString&)
{
	using namespace InventoryTabsTests;
	const TCHAR* Tags[] = {TEXT("GameItem.Equipment.Weapon.GreatSword"), TEXT("GameItem.Consume.Potion.Red.Large"), TEXT("GameItem.Material.Progression.Ore")};
	for (int32 Tab = 0; Tab < 3; ++Tab)
	{
		auto* Definition = NewObject<URPGItemDefinition>();
		Definition->ItemTag = FGameplayTag::RequestGameplayTag(FName(Tags[Tab]), false);
		if (!TestTrue(TEXT("Authored native definition tag available"), Definition->ItemTag.IsValid())) return false;
		Definition->ItemCategory = static_cast<EItemCategory>(Tab);
		FRPGItemDefinitionRegistry Definitions;
		TestTrue(TEXT("Definition registered"), Definitions.RegisterDefinition(*Definition));
		FRPGInMemoryItemRepository Repo;
		FRPGItemPickupGrantRequest Request;
		Request.RequestId = FGuid::NewGuid(); Request.ItemId = FGuid::NewGuid(); Request.Owner = Owner;
		Request.Quantity = 1; Request.CreationSource = TEXT("WorldPickup");
		FRPGItemRepositoryCommitRequest Plan;
		TestTrue(TEXT("Pickup planned in empty bag"), FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition, Repo, Definitions, Plan));
		if (Plan.Mutations.IsEmpty()) return false;
		TestEqual(TEXT("Pickup uses own tab's first slot"), Plan.Mutations[0].NewRecord.GetLocation().SlotIndex, Tab * 80);
		for (int32 Slot = Tab * 80; Slot < (Tab + 1) * 80; ++Slot) Request.ReservedInventorySlots.Add(Slot);
		TestFalse(TEXT("Cannot borrow another tab when all own slots reserved"), FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition, Repo, Definitions, Plan));
		Request.ReservedInventorySlots.Reset();
		TArray<FRPGItemRecord> Full;
		for (int32 Slot = Tab * 80; Slot < (Tab + 1) * 80; ++Slot) Full.Add(Make(Tags[Tab], Slot));
		TestTrue(TEXT("Full own tab fixture"), Seed(Repo, Full));
		TestFalse(TEXT("Cannot borrow another tab when own tab full"), FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition, Repo, Definitions, Plan));
	}
	return true;
}
#endif
