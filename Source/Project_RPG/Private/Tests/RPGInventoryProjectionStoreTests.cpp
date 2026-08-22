#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Projection/RPGInventoryProjectionStore.h"
#include "Type/RPGEnumTypes.h"

namespace RPGInventoryProjectionStoreTests
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
	const ERPGItemLifecycleState Lifecycle,
	FRPGItemRecord& OutRecord)
{
	FRPGItemInstanceState State;
	if (!FRPGItemInstanceState::TryRestore(
		ItemId,
		1234,
		Quantity,
		{},
		{},
		State))
	{
		return false;
	}

	FRPGItemLocation Location;
	if (Lifecycle == ERPGItemLifecycleState::Active)
	{
		Location.ContainerType = ContainerType;
		Location.ContainerId = ContainerType ==
			ERPGItemContainerType::Inventory
			? TEXT("inventory:projection-store")
			: TEXT("equipment:projection-store");
		Location.SlotIndex = SlotIndex;
	}
	else
	{
		Location = FRPGItemLocation::MakeTerminal();
	}

	FRPGItemRecordMetadata Metadata;
	return FRPGItemRecord::TryRestore(
		FPrimaryAssetId(
			FPrimaryAssetType(FName(TEXT("RPGItemDefinition"))),
			FName(TEXT("ProjectionStoreItem"))),
		1,
		Owner,
		Location,
		State,
		Revision,
		Lifecycle,
		Metadata,
		OutRecord);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGInventoryProjectionStoreMutationTest,
	"ProjectRPG.Item.Projection.CommitMutationsPreserveUnaffectedItems",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGInventoryProjectionStoreMutationTest::RunTest(
	const FString& Parameters)
{
	using namespace RPGInventoryProjectionStoreTests;
	const FRPGItemOwnerRef Owner = MakeOwner(
		TEXT("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
	const FGuid EquippedItemId = FGuid::NewGuid();
	const FGuid ConsumedItemId = FGuid::NewGuid();
	const FGuid UnaffectedItemId = FGuid::NewGuid();

	FRPGItemRecord EquippedInitial;
	FRPGItemRecord ConsumedInitial;
	FRPGItemRecord UnaffectedInitial;
	TestTrue(TEXT("First initial record is valid"),
		MakeRecord(
			Owner,
			EquippedItemId,
			ERPGItemContainerType::Inventory,
			0,
			1,
			1,
			ERPGItemLifecycleState::Active,
			EquippedInitial));
	TestTrue(TEXT("Second initial record is valid"),
		MakeRecord(
			Owner,
			ConsumedItemId,
			ERPGItemContainerType::Inventory,
			1,
			2,
			1,
			ERPGItemLifecycleState::Active,
			ConsumedInitial));
	TestTrue(TEXT("Unaffected initial record is valid"),
		MakeRecord(
			Owner,
			UnaffectedItemId,
			ERPGItemContainerType::Inventory,
			2,
			1,
			1,
			ERPGItemLifecycleState::Active,
			UnaffectedInitial));

	FRPGInventoryProjectionStore Store;
	TArray<FRPGInventoryProjectionEntry> Entries;
	FString Error;
	TestTrue(
		TEXT("A full authoritative load initializes the store"),
		Store.Replace(
			Owner,
			{EquippedInitial, ConsumedInitial, UnaffectedInitial},
			Entries,
			&Error));
	TestEqual(TEXT("All inventory items are initially projected"),
		Entries.Num(), 3);

	FRPGItemRecord EquippedMutation;
	FRPGItemRecord ConsumedMutation;
	TestTrue(TEXT("Equipment mutation is valid"),
		MakeRecord(
			Owner,
			EquippedItemId,
			ERPGItemContainerType::Equipment,
			static_cast<int32>(
				EEquipmentSlotType::Weapon_Primary_R),
			1,
			2,
			ERPGItemLifecycleState::Active,
			EquippedMutation));
	TestTrue(TEXT("Consumed tombstone mutation is valid"),
		MakeRecord(
			Owner,
			ConsumedItemId,
			ERPGItemContainerType::Terminal,
			INDEX_NONE,
			0,
			2,
			ERPGItemLifecycleState::Consumed,
			ConsumedMutation));
	TestTrue(
		TEXT("A successful commit delta updates the cached snapshot"),
		Store.ApplyMutations(
			Owner,
			{EquippedMutation, ConsumedMutation},
			Entries,
			&Error));
	TestEqual(
		TEXT("Equipment and terminal records leave inventory projection"),
		Entries.Num(),
		1);
	if (Entries.Num() == 1)
	{
		TestEqual(
			TEXT("An unrelated inventory item is preserved"),
			Entries[0].GetItemId(),
			UnaffectedItemId);
	}
	TestEqual(
		TEXT("The authoritative cache retains all record lifecycles"),
		Store.NumAuthoritativeRecords(),
		3);

	FRPGItemRecord StaleMutation;
	MakeRecord(
		Owner,
		UnaffectedItemId,
		ERPGItemContainerType::Inventory,
		4,
		1,
		0,
		ERPGItemLifecycleState::Active,
		StaleMutation);
	TestTrue(
		TEXT("An out-of-order callback is accepted as a no-op"),
		Store.ApplyMutations(Owner, {StaleMutation}, Entries, &Error));
	TestEqual(TEXT("A stale receipt preserves the current snapshot"),
		Entries.Num(), 1);
	if (Entries.Num() == 1)
	{
		TestEqual(TEXT("A stale receipt cannot roll back the slot"),
			Entries[0].GetSlotIndex(), 2);
	}

	const FRPGItemOwnerRef ForeignOwner = MakeOwner(
		TEXT("ffffffff-1111-2222-3333-444444444444"));
	FRPGItemRecord ForeignMutation;
	MakeRecord(
		ForeignOwner,
		FGuid::NewGuid(),
		ERPGItemContainerType::Inventory,
		0,
		1,
		1,
		ERPGItemLifecycleState::Active,
		ForeignMutation);
	TestFalse(
		TEXT("A foreign commit cannot contaminate the projection cache"),
		Store.ApplyMutations(Owner, {ForeignMutation}, Entries, &Error));
	TestEqual(
		TEXT("Rejected commits leave the authoritative cache intact"),
		Store.NumAuthoritativeRecords(),
		3);
	return true;
}

#endif
