#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Persistence/RPGInMemoryItemRepository.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Transaction/RPGItemPickupGrantPlanner.h"
#include "RPGItemTags.h"

namespace RPGItemPickupGrantPlannerTests
{
class FRecordSource final : public IRPGItemRecordSource
{
public:
	TArray<FRPGItemRecord> Items;

	virtual bool Find(const FGuid& ItemId,
		FRPGItemRecord& OutRecord) const override
	{
		const FRPGItemRecord* Item = Items.FindByPredicate(
			[&ItemId](const FRPGItemRecord& Candidate)
			{ return Candidate.GetItemId() == ItemId; });
		if (!Item) return false;
		OutRecord = *Item;
		return true;
	}

	virtual bool FindAtLocation(const FRPGItemOwnerRef& Owner,
		const FRPGItemLocation& Location,
		FRPGItemRecord& OutRecord) const override
	{
		const FRPGItemRecord* Item = Items.FindByPredicate(
			[&Owner, &Location](const FRPGItemRecord& Candidate)
			{
				return Candidate.IsActive() && Candidate.GetOwner() == Owner &&
					Candidate.GetLocation() == Location;
			});
		if (!Item) return false;
		OutRecord = *Item;
		return true;
	}

	virtual void FindByOwner(const FRPGItemOwnerRef& Owner,
		TArray<FRPGItemRecord>& OutRecords) const override
	{
		OutRecords.Reset();
		for (const FRPGItemRecord& Item : Items)
		{
			if (Item.GetOwner() == Owner) OutRecords.Add(Item);
		}
	}
};

URPGItemDefinition* MakeDefinition()
{
	URPGItemDefinition* Definition = NewObject<URPGItemDefinition>(
		GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(),
			URPGItemDefinition::StaticClass(), TEXT("PickupGrantDefinition")));
	Definition->ItemTag = RPGGameplayTags::GameItem_Consume_Potion_Red_Large;
	Definition->ItemCategory = EItemCategory::Consume;
	Definition->DisplayName = FText::FromString(TEXT("Pickup Grant Test"));
	Definition->MaxStackSize = 10;
	Definition->DefinitionVersion = 2;
	return Definition;
}

FRPGItemPickupGrantRequest MakeRequest()
{
	FRPGItemPickupGrantRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.ItemId = FGuid::NewGuid();
	Request.Owner.Type = ERPGItemOwnerType::Character;
	Request.Owner.OwnerId = TEXT("character:pickup-test");
	Request.Quantity = 4;
	Request.GenerationSeed = 12345;
	Request.CreationSource = TEXT("WorldPickup.test");
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemPickupGrantPlansStableMintTest,
	"ProjectRPG.Item.PickupGrant.PlansStableMint",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemPickupGrantPlansStableMintTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemPickupGrantPlannerTests;
	URPGItemDefinition* Definition = MakeDefinition();
	FRPGItemDefinitionRegistry Definitions;
	TestTrue(TEXT("Native definition registers"),
		Definitions.RegisterDefinition(*Definition));
	FRecordSource Records;
	const FRPGItemPickupGrantRequest Request = MakeRequest();
	FRPGItemRepositoryCommitRequest First;
	FRPGItemRepositoryCommitRequest Retry;
	TestTrue(TEXT("Pickup grant plans"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition,
			Records, Definitions, First));
	TestTrue(TEXT("Retry plans"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition,
			Records, Definitions, Retry));
	TestEqual(TEXT("Only one new record is planned"), First.Mutations.Num(), 1);
	if (First.Mutations.Num() != 1 || Retry.Mutations.Num() != 1)
	{
		return false;
	}
	const FRPGItemRecord& Item = First.Mutations[0].NewRecord;
	TestEqual(TEXT("Expected revision is zero"),
		First.Mutations[0].ExpectedRevision, static_cast<int64>(0));
	TestEqual(TEXT("Server-issued item identity is preserved"),
		Item.GetItemId(), Request.ItemId);
	TestEqual(TEXT("Whole pickup stack is preserved"),
		Item.GetQuantity(), Request.Quantity);
	TestEqual(TEXT("First free inventory slot is chosen"),
		Item.GetLocation().SlotIndex, 0);
	TestEqual(TEXT("Retry fingerprint remains stable"),
		First.CommandFingerprint, Retry.CommandFingerprint);
	TestEqual(TEXT("Retry uses the same item identity"),
		Item.GetItemId(), Retry.Mutations[0].NewRecord.GetItemId());
	TestEqual(TEXT("Creation source is auditable"),
		Item.GetMetadata().CreationSource, Request.CreationSource);
	FRPGInMemoryItemRepository Repository;
	TestTrue(TEXT("Initial grant commit succeeds"),
		Repository.Commit(First).Status ==
			ERPGItemRepositoryCommitStatus::Committed);
	TestTrue(TEXT("Identical retry returns one receipt"),
		Repository.Commit(Retry).Status ==
			ERPGItemRepositoryCommitStatus::AlreadyCommitted);
	FRPGItemRecord Stored;
	TestTrue(TEXT("Granted identity is persisted"),
		Repository.Find(Request.ItemId, Stored));
	TestEqual(TEXT("Retry does not increment the revision"),
		Stored.GetRevision(), static_cast<int64>(1));
	FRPGItemPickupGrantRequest ChangedSeed = Request;
	++ChangedSeed.GenerationSeed;
	FRPGItemRepositoryCommitRequest ChangedSeedCommit;
	TestTrue(TEXT("Changed seed still creates a well-formed plan"),
		FRPGItemPickupGrantPlanner::TryPlan(ChangedSeed, *Definition,
			Records, Definitions, ChangedSeedCommit));
	TestNotEqual(TEXT("Seed changes the command fingerprint"),
		First.CommandFingerprint, ChangedSeedCommit.CommandFingerprint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemPickupGrantRejectsInvalidMintTest,
	"ProjectRPG.Item.PickupGrant.RejectsInvalidMint",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemPickupGrantRejectsInvalidMintTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemPickupGrantPlannerTests;
	URPGItemDefinition* Definition = MakeDefinition();
	FRPGItemDefinitionRegistry Definitions;
	Definitions.RegisterDefinition(*Definition);
	FRecordSource Records;
	FRPGItemPickupGrantRequest Request = MakeRequest();
	FRPGItemRepositoryCommitRequest Commit;

	Request.Quantity = 11;
	TestFalse(TEXT("Overstacked pickup is rejected"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition,
			Records, Definitions, Commit));
	Request.Quantity = 4;
	Request.Owner.Type = ERPGItemOwnerType::World;
	TestFalse(TEXT("World owner cannot receive an inventory grant"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition,
			Records, Definitions, Commit));
	Request.Owner.Type = ERPGItemOwnerType::Character;
	Definition->DefinitionVersion = 3;
	TestFalse(TEXT("Unregistered definition version is rejected"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition,
			Records, Definitions, Commit));
	Definition->DefinitionVersion = 2;

	for (int32 Slot = 0; Slot < RPGItemInventoryPolicy::SlotCapacity; ++Slot)
	{
		FRPGItemInstanceState State;
		Definition->BuildInstanceState(1, Slot, State);
		FRPGItemLocation Location;
		Location.ContainerType = ERPGItemContainerType::Inventory;
		Location.ContainerId = Request.Owner.OwnerId;
		Location.SlotIndex = Slot;
		FRPGItemRecord Item;
		if (!FRPGItemRecord::TryCreate(Definition->GetPrimaryAssetId(),
			Definition->GetDefinitionVersion(), Request.Owner,
			Location, State, FRPGItemRecordMetadata(), Item))
		{
			AddError(TEXT("Could not prepare occupied inventory slots."));
			return false;
		}
		Records.Items.Add(MoveTemp(Item));
	}
	TestFalse(TEXT("Full inventory cannot accept a pickup"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition,
			Records, Definitions, Commit));
	TestEqual(TEXT("Rejected pickup produces no mutations"),
		Commit.Mutations.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupReservedSlotsPlannerTest,
	"ProjectRPG.Item.PickupGrant.SkipsReservedSlotsWithoutChangingFrozenIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPickupReservedSlotsPlannerTest::RunTest(const FString&)
{
	using namespace RPGItemPickupGrantPlannerTests;
	auto* Definition = MakeDefinition();
	FRPGItemDefinitionRegistry Definitions;
	Definitions.RegisterDefinition(*Definition);
	FRecordSource Records;
	auto Request = MakeRequest();
	Request.ReservedInventorySlots = {0, 2};
	FRPGItemRepositoryCommitRequest First, SameSlot;
	if (!TestTrue(TEXT("Plan around live server reservations"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition, Records, Definitions, First))) return false;
	TestEqual(TEXT("Lowest genuinely free slot"), First.Mutations[0].NewRecord.GetLocation().SlotIndex, 1);
	TestEqual(TEXT("Original item identity"), First.Mutations[0].NewRecord.GetItemId(), Request.ItemId);
	Request.ReservedInventorySlots.Add(79);
	if (!TestTrue(TEXT("Another unrelated reservation can be present"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition, Records, Definitions, SameSlot))) return false;
	TestEqual(TEXT("Only chosen slot participates in frozen fingerprint"), First.CommandFingerprint, SameSlot.CommandFingerprint);
	for (int32 Slot = 0; Slot < RPGItemInventoryPolicy::SlotCapacity; ++Slot) Request.ReservedInventorySlots.Add(Slot);
	TestFalse(TEXT("All empty slots reserved rejects before creating a grant"),
		FRPGItemPickupGrantPlanner::TryPlan(Request, *Definition, Records, Definitions, SameSlot));
	TestTrue(TEXT("No stale mutations from rejected planning"), SameSlot.Mutations.IsEmpty());
	TestTrue(TEXT("Original earlier plan is not replanned or overwritten"),
		First.RequestId == Request.RequestId && First.Mutations[0].NewRecord.GetLocation().SlotIndex == 1);
	return true;
}

#endif
