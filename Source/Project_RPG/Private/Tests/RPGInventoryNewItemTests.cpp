#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Item/Projection/RPGInventoryNewItemTracker.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"

namespace RPGInventoryNewItemTests
{
FRPGInventoryProjectionEntry Item(const FGuid Id, const int32 Quantity, const int32 Slot = 0,
	const ERPGItemContainerType Container = ERPGItemContainerType::Inventory, const int64 Revision = 1)
{
	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	FRPGItemLocation Location;
	Location.ContainerType = Container;
	Location.ContainerId = Owner.OwnerId;
	Location.SlotIndex = Slot;
	FRPGItemInstanceState State;
	check(FRPGItemInstanceState::TryRestore(Id, 123, Quantity, {}, {}, State));
	FRPGItemRecord Record;
	check(FRPGItemRecord::TryRestore(FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("HighlightTest")),
		1, Owner, Location, State, Revision, ERPGItemLifecycleState::Active, {}, Record));
	TArray<FRPGInventoryProjectionEntry> Entries;
	check(FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner, {Record}, Entries));
	check(Entries.Num() == 1);
	return Entries[0];
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryNewAcquisitionTest,
	"ProjectRPG.Item.NewItemHighlight.AcquisitionAndAcknowledgement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryNewAcquisitionTest::RunTest(const FString& Parameters)
{
	using namespace RPGInventoryNewItemTests;
	FRPGInventoryNewItemTracker Tracker;
	const FGuid Existing = FGuid::NewGuid(), NewId = FGuid::NewGuid();
	Tracker.Observe({Item(Existing, 4)});
	TestFalse(TEXT("Initial owned items are already seen"), Tracker.IsUnseen(Existing));
	Tracker.Observe({Item(Existing, 4), Item(NewId, 1, 1)});
	TestTrue(TEXT("Acquired ID attracts attention"), Tracker.IsUnseen(NewId));
	TestFalse(TEXT("Unchanged ID does not"), Tracker.IsUnseen(Existing));
	TestTrue(TEXT("Explicit acknowledgement clears once"), Tracker.MarkSeen(NewId));
	TestFalse(TEXT("Second acknowledgement has no notification"), Tracker.MarkSeen(NewId));
	Tracker.Observe({Item(Existing, 4), Item(NewId, 1, 1)});
	TestFalse(TEXT("Duplicate snapshot never rearms NEW"), Tracker.IsUnseen(NewId));
	Tracker.Observe({Item(Existing, 6, 0, ERPGItemContainerType::Inventory, 2), Item(NewId, 1, 1)});
	TestTrue(TEXT("New units in an existing stack attract attention"), Tracker.IsUnseen(Existing));
	Tracker.MarkSeen(Existing);
	Tracker.Observe({Item(Existing, 5, 0, ERPGItemContainerType::Inventory, 3), Item(NewId, 1, 1)});
	TestFalse(TEXT("Consumption does not rearm"), Tracker.IsUnseen(Existing));
	Tracker.Observe({Item(Existing, 6, 0, ERPGItemContainerType::Inventory, 4), Item(NewId, 1, 1)});
	TestTrue(TEXT("Another acquisition rearms a seen stack"), Tracker.IsUnseen(Existing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryNewReorganizationTest,
	"ProjectRPG.Item.NewItemHighlight.ReorganizationDoesNotAcquire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryNewReorganizationTest::RunTest(const FString& Parameters)
{
	using namespace RPGInventoryNewItemTests;
	FRPGInventoryNewItemTracker Tracker;
	const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
	Tracker.Observe({Item(A, 10)});
	Tracker.Observe({Item(A, 10, 3, ERPGItemContainerType::Inventory, 2)});
	TestFalse(TEXT("Move or sort does not mark NEW"), Tracker.IsUnseen(A));
	Tracker.Observe({Item(A, 6, 3, ERPGItemContainerType::Inventory, 3), Item(B, 4, 2)});
	TestFalse(TEXT("Split source is seen"), Tracker.IsUnseen(A));
	TestFalse(TEXT("Split-created ID is seen"), Tracker.IsUnseen(B));
	Tracker.Observe({Item(A, 10, 3, ERPGItemContainerType::Inventory, 4)});
	TestFalse(TEXT("Merge quantity increase is not an acquisition"), Tracker.IsUnseen(A));
	Tracker.Observe({Item(A, 10, 0, ERPGItemContainerType::Equipment, 5)});
	Tracker.Observe({Item(A, 10, 2, ERPGItemContainerType::Inventory, 6)});
	TestFalse(TEXT("Unequip does not mark NEW"), Tracker.IsUnseen(A));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryNewLifetimeTest,
	"ProjectRPG.Item.NewItemHighlight.EmptyBaselineLifetimeAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryNewLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace RPGInventoryNewItemTests;
	FRPGInventoryNewItemTracker Tracker;
	const FGuid A = FGuid::NewGuid();
	Tracker.Observe({});
	Tracker.Observe({Item(A, 1)});
	TestTrue(TEXT("Acquisition after an empty initial bag is visible"), Tracker.IsUnseen(A));
	Tracker.Observe({Item(A, 1, 3, ERPGItemContainerType::Inventory, 2)});
	TestTrue(TEXT("An unread item follows its ID across slots"), Tracker.IsUnseen(A));
	Tracker.Observe({Item(A, 1, 0, ERPGItemContainerType::Equipment, 3)});
	TestFalse(TEXT("Equipping removes bag attention"), Tracker.IsUnseen(A));
	Tracker.Observe({});
	Tracker.Observe({Item(A, 1)});
	TestTrue(TEXT("An externally returned item can attract attention"), Tracker.IsUnseen(A));
	Tracker.Observe({});
	TestFalse(TEXT("Removed item does not leak unread state"), Tracker.IsUnseen(A));
	Tracker.Reset();
	Tracker.Observe({Item(A, 1)});
	TestFalse(TEXT("A new identity/session establishes a clean baseline"), Tracker.IsUnseen(A));
	return true;
}

#endif
