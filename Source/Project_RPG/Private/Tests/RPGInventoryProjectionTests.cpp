#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "RPGItemTags.h"

namespace RPGInventoryProjectionTests
{
FRPGItemOwnerRef MakeOwner(const TCHAR* OwnerId)
{
	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = OwnerId;
	return Owner;
}

bool MakeRecord(
	const FRPGItemOwnerRef& Owner,
	const FGuid& ItemId,
	const ERPGItemContainerType ContainerType,
	const int32 SlotIndex,
	const int32 Quantity,
	const int64 Revision,
	FRPGItemRecord& OutRecord)
{
	FGameplayTagContainer InstanceTags;
	InstanceTags.AddTag(RPGGameplayTags::GameItem_Craft_fruit);
	TArray<FRPGItemStatValue> StatValues;
	FRPGItemStatValue& Stat = StatValues.AddDefaulted_GetRef();
	Stat.StatTag = RPGGameplayTags::Fragment_StatMod_1;
	Stat.Value = 12.5f;

	FRPGItemInstanceState State;
	if (!FRPGItemInstanceState::TryRestore(
		ItemId,
		7919,
		Quantity,
		InstanceTags,
		StatValues,
		State))
	{
		return false;
	}

	FRPGItemLocation Location;
	Location.ContainerType = ContainerType;
	Location.ContainerId = ContainerType == ERPGItemContainerType::Inventory
		? Owner.OwnerId
		: TEXT("server-only-container");
	Location.SlotIndex = SlotIndex;

	FRPGItemRecordMetadata Metadata;
	Metadata.BindState = ERPGItemBindState::CharacterBound;
	Metadata.Durability.Current = 12;
	Metadata.Durability.Maximum = 20;
	Metadata.bLocked = true;
	return FRPGItemRecord::TryRestore(
		FPrimaryAssetId(
			FPrimaryAssetType(FName(TEXT("RPGItemDefinition"))),
			FName(TEXT("ProjectionSword"))),
		3,
		Owner,
		Location,
		State,
		Revision,
		ERPGItemLifecycleState::Active,
		Metadata,
		OutRecord);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGInventoryProjectionMapperTest,
	"ProjectRPG.Item.Projection.MapsOnlyOwnedInventoryRecords",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGInventoryProjectionMapperTest::RunTest(const FString& Parameters)
{
	using namespace RPGInventoryProjectionTests;
	const FRPGItemOwnerRef Owner = MakeOwner(
		TEXT("10203040-5060-7080-90a0-b0c0d0e0f001"));
	const FGuid InventoryItemId = FGuid::NewGuid();
	FRPGItemRecord InventoryRecord;
	FRPGItemRecord TradeRecord;
	constexpr int64 LargeRevision = 9007199254740993LL;
	TestTrue(
		TEXT("An inventory record can be restored"),
		MakeRecord(
			Owner,
			InventoryItemId,
			ERPGItemContainerType::Inventory,
			4,
			2,
			LargeRevision,
			InventoryRecord));
	TestTrue(
		TEXT("A server-only trade record can be restored"),
		MakeRecord(
			Owner,
			FGuid::NewGuid(),
			ERPGItemContainerType::Trade,
			0,
			1,
			7,
			TradeRecord));

	TArray<FRPGInventoryProjectionEntry> Snapshot;
	FString Error;
	TestTrue(
		TEXT("The owned records produce a projection"),
		FRPGInventoryProjectionMapper::BuildInventorySnapshot(
			Owner,
			{TradeRecord, InventoryRecord},
			Snapshot,
			&Error));
	TestEqual(TEXT("Only inventory records are projected"), Snapshot.Num(), 1);
	if (Snapshot.Num() == 1)
	{
		const FRPGInventoryProjectionEntry& Entry = Snapshot[0];
		TestEqual(TEXT("Identity is projected"), Entry.GetItemId(), InventoryItemId);
		TestEqual(TEXT("Slot is projected"), Entry.GetSlotIndex(), 4);
		TestEqual(TEXT("Quantity is projected"), Entry.GetQuantity(), 2);
		TestEqual(
			TEXT("Large revision remains exact"),
			Entry.GetRevision(),
			LargeRevision);
		TestEqual(
			TEXT("Bind state is projected"),
			Entry.GetBindState(),
			ERPGItemBindState::CharacterBound);
		TestTrue(TEXT("Lock state is projected"), Entry.IsLocked());
		TestEqual(
			TEXT("Rolled stats are projected"),
			Entry.GetRolledStats().Num(),
			1);
	}

	const FRPGItemOwnerRef ForeignOwner = MakeOwner(
		TEXT("10203040-5060-7080-90a0-b0c0d0e0f999"));
	FRPGItemRecord ForeignRecord;
	TestTrue(
		TEXT("A foreign record can be restored"),
		MakeRecord(
			ForeignOwner,
			FGuid::NewGuid(),
			ERPGItemContainerType::Inventory,
			1,
			1,
			1,
			ForeignRecord));
	TestFalse(
		TEXT("A foreign record rejects the whole snapshot"),
		FRPGInventoryProjectionMapper::BuildInventorySnapshot(
			Owner,
			{ForeignRecord},
			Snapshot,
			&Error));
	TestEqual(TEXT("Rejected snapshots expose no entries"), Snapshot.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGInventoryProjectionReconcileTest,
	"ProjectRPG.Item.Projection.ReconcilesByStableIdentity",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGInventoryProjectionReconcileTest::RunTest(
	const FString& Parameters)
{
	using namespace RPGInventoryProjectionTests;
	const FRPGItemOwnerRef Owner = MakeOwner(
		TEXT("11223344-5566-7788-99aa-bbccddeeff00"));
	const FGuid ItemId = FGuid::NewGuid();
	FRPGItemRecord InitialRecord;
	FRPGItemRecord UpdatedRecord;
	TestTrue(
		TEXT("The initial record can be restored"),
		MakeRecord(
			Owner,
			ItemId,
			ERPGItemContainerType::Inventory,
			2,
			2,
			10,
			InitialRecord));
	TestTrue(
		TEXT("The updated record can be restored"),
		MakeRecord(
			Owner,
			ItemId,
			ERPGItemContainerType::Inventory,
			2,
			3,
			11,
			UpdatedRecord));

	TArray<FRPGInventoryProjectionEntry> InitialSnapshot;
	TArray<FRPGInventoryProjectionEntry> UpdatedSnapshot;
	TestTrue(
		TEXT("The initial snapshot is valid"),
		FRPGInventoryProjectionMapper::BuildInventorySnapshot(
			Owner,
			{InitialRecord},
			InitialSnapshot));
	TestTrue(
		TEXT("The updated snapshot is valid"),
		FRPGInventoryProjectionMapper::BuildInventorySnapshot(
			Owner,
			{UpdatedRecord},
			UpdatedSnapshot));

	FRPGInventoryProjectionList List;
	bool bChanged = false;
	TestTrue(
		TEXT("The initial snapshot reconciles"),
		List.Reconcile(InitialSnapshot, bChanged));
	TestTrue(TEXT("The initial snapshot adds an entry"), bChanged);
	TestTrue(
		TEXT("An identical snapshot reconciles"),
		List.Reconcile(InitialSnapshot, bChanged));
	TestFalse(TEXT("An identical snapshot produces no delta"), bChanged);
	TestTrue(
		TEXT("The updated snapshot reconciles"),
		List.Reconcile(UpdatedSnapshot, bChanged));
	TestTrue(TEXT("A payload update produces a delta"), bChanged);
	const FRPGInventoryProjectionEntry* UpdatedEntry = List.Find(ItemId);
	TestNotNull(TEXT("Stable identity is retained"), UpdatedEntry);
	if (UpdatedEntry)
	{
		TestEqual(TEXT("Quantity is updated"), UpdatedEntry->GetQuantity(), 3);
		TestEqual(TEXT("Revision is updated"), UpdatedEntry->GetRevision(), 11LL);
	}

	TestTrue(
		TEXT("An empty snapshot reconciles"),
		List.Reconcile({}, bChanged));
	TestTrue(TEXT("The missing entry is removed"), bChanged);
	TestEqual(TEXT("The projection is empty"), List.GetEntries().Num(), 0);
	return true;
}

#endif
