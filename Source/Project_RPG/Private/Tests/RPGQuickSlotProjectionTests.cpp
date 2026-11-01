#if WITH_DEV_AUTOMATION_TESTS

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Misc/AutomationTest.h"

struct FRPGQuickSlotProjectionTestAccess
{
	static void Restart(ARPGPlayerController& Controller, APawn* Pawn)
	{
		Controller.ClientRestart_Implementation(Pawn);
	}
};

namespace RPGQuickSlotProjectionTests
{
struct FFixture
{
	FRPGItemActionPolicyRegistry Policies;
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	UQuickSlotComponent* QuickSlots = nullptr;
	FRPGItemOwnerRef Owner;
	FPrimaryAssetId DefinitionId = FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("QuickSlotPotion"));

	FFixture()
	{
		Owner.Type = ERPGItemOwnerType::Character;
		Owner.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
		FRPGItemActionPolicy Policy;
		Policy.DefinitionId = DefinitionId;
		Policy.DefinitionVersion = 3;
		Policy.QuantityPerUse = 1;
		Policies.RegisterPolicy(Policy);
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>();
		APawn* Pawn = World->SpawnActor<APawn>();
		if (!Controller || !Pawn) return;
		Controller->Possess(Pawn);
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection);
		Projection->RegisterComponent();
		QuickSlots = NewObject<UQuickSlotComponent>(Pawn);
		Pawn->AddInstanceComponent(QuickSlots);
		QuickSlots->RegisterComponent();
		QuickSlots->BindInventoryProjection(Projection, &Policies);
	}

	~FFixture()
	{
		if (QuickSlots) QuickSlots->BindInventoryProjection(nullptr);
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}

	bool MakeRecord(FGuid ItemId, int32 Quantity, int64 Revision,
		ERPGItemContainerType Container, FRPGItemRecord& OutRecord,
		int32 Version = 3) const
	{
		FRPGItemInstanceState State;
		if (!FRPGItemInstanceState::TryRestore(ItemId, 17, Quantity, {}, {}, State)) return false;
		FRPGItemLocation Location;
		Location.ContainerType = Container;
		Location.ContainerId = Owner.OwnerId;
		Location.SlotIndex = Container == ERPGItemContainerType::Equipment
			? static_cast<int32>(EEquipmentSlotType::Head) : 4;
		const bool bActive = Quantity > 0;
		if (!bActive) Location = FRPGItemLocation::MakeTerminal();
		return FRPGItemRecord::TryRestore(DefinitionId, Version, Owner, Location,
			State, Revision, bActive ? ERPGItemLifecycleState::Active : ERPGItemLifecycleState::Consumed,
			{}, OutRecord);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuickSlotRegistrationTest,
	"ProjectRPG.Item.QuickSlot.ValidatesOwnedConsumableIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGQuickSlotRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace RPGQuickSlotProjectionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Quick slot fixture"), Fixture.QuickSlots)) return false;
	const FGuid ItemId = FGuid::NewGuid();
	FRPGItemRecord Record;
	if (!TestTrue(TEXT("Restore inventory potion"), Fixture.MakeRecord(ItemId, 3, 7,
		ERPGItemContainerType::Inventory, Record))) return false;
	TestTrue(TEXT("Initialize owned projection"), Fixture.Projection->ApplyAuthoritativeRecords(
		Fixture.Owner, {Record}));
	Fixture.QuickSlots->SetItemSlotById(0, ItemId);
	TestEqual(TEXT("Shortcut stores stable ID"), Fixture.QuickSlots->GetItemSlotContent(0)->ItemId, ItemId);
	TestNull(TEXT("No legacy instance is stored"), Fixture.QuickSlots->GetItemSlotContent(0)->Item.Get());
	Fixture.QuickSlots->SetItemSlotById(0, FGuid::NewGuid());
	TestEqual(TEXT("Unknown ID cannot replace assignment"), Fixture.QuickSlots->GetItemSlotContent(0)->ItemId, ItemId);
	Fixture.QuickSlots->SetItemSlotById(-1, ItemId);
	Fixture.QuickSlots->SetItemSlotById(Fixture.QuickSlots->GetMaxItemSlots(), ItemId);
	TestEqual(TEXT("Invalid slots leave assignment intact"), Fixture.QuickSlots->GetItemSlotContent(0)->ItemId, ItemId);
	APlayerController* OtherController = Fixture.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Other controller"), OtherController)) return false;
	URPGInventoryProjectionComponent* OtherProjection = NewObject<URPGInventoryProjectionComponent>(OtherController);
	OtherController->AddInstanceComponent(OtherProjection);
	OtherProjection->RegisterComponent();
	Fixture.QuickSlots->BindInventoryProjection(OtherProjection, &Fixture.Policies);
	FRPGInventoryProjectionEntry Entry;
	TestTrue(TEXT("Foreign projection cannot replace the owned binding"), Fixture.QuickSlots->FindAssignedItem(0, Entry));
	TestEqual(TEXT("Owned item remains assigned"), Entry.GetItemId(), ItemId);

	FRPGItemRecord Equipped;
	TestTrue(TEXT("Restore equipped record"), Fixture.MakeRecord(FGuid::NewGuid(), 1, 1,
		ERPGItemContainerType::Equipment, Equipped));
	TestTrue(TEXT("Apply equipment record"), Fixture.Projection->ApplyAuthoritativeMutationRecords(Fixture.Owner, {Equipped}));
	Fixture.QuickSlots->SetItemSlotById(1, Equipped.GetState().GetInstanceId());
	TestTrue(TEXT("Equipment cannot be registered as a consumable shortcut"), Fixture.QuickSlots->GetItemSlotContent(1)->IsEmpty());
	FRPGItemRecord WrongVersion;
	TestTrue(TEXT("Restore another definition version"), Fixture.MakeRecord(ItemId, 3, 8,
		ERPGItemContainerType::Inventory, WrongVersion, 4));
	TestTrue(TEXT("Update definition version"), Fixture.Projection->ApplyAuthoritativeMutationRecords(Fixture.Owner, {WrongVersion}));
	Fixture.QuickSlots->ClearSlot(false, 0);
	Fixture.QuickSlots->SetItemSlotById(0, ItemId);
	TestTrue(TEXT("Unmatched action policy version rejects assignment"), Fixture.QuickSlots->GetItemSlotContent(0)->IsEmpty());
	FRPGItemActionPolicy NonConsumablePolicy;
	NonConsumablePolicy.DefinitionId = Fixture.DefinitionId;
	NonConsumablePolicy.DefinitionVersion = 4;
	TestTrue(TEXT("Register a valid nonconsumable policy"), Fixture.Policies.RegisterPolicy(NonConsumablePolicy));
	Fixture.QuickSlots->SetItemSlotById(0, ItemId);
	TestTrue(TEXT("Nonconsumables cannot occupy item shortcuts"), Fixture.QuickSlots->GetItemSlotContent(0)->IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuickSlotProjectionLifecycleTest,
	"ProjectRPG.Item.QuickSlot.FollowsProjectionQuantityAndRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGQuickSlotProjectionLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace RPGQuickSlotProjectionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Quick slot fixture"), Fixture.QuickSlots)) return false;
	const FGuid ItemId = FGuid::NewGuid();
	constexpr int64 Revision = 9007199254740993LL;
	FRPGItemRecord Record;
	if (!TestTrue(TEXT("Restore consumable"), Fixture.MakeRecord(ItemId, 3, Revision,
		ERPGItemContainerType::Inventory, Record))) return false;
	TestTrue(TEXT("Initialize projection"), Fixture.Projection->ApplyAuthoritativeRecords(Fixture.Owner, {Record}));
	Fixture.QuickSlots->SetItemSlotById(0, ItemId);
	Fixture.QuickSlots->SetItemSlotById(1, ItemId);
	FRPGInventoryProjectionEntry Entry;
	TestTrue(TEXT("Shortcut resolves projection"), Fixture.QuickSlots->FindAssignedItem(0, Entry));
	TestEqual(TEXT("Revision remains int64"), Entry.GetRevision(), Revision);
	TestTrue(TEXT("Restore quantity mutation"), Fixture.MakeRecord(ItemId, 2, Revision + 1,
		ERPGItemContainerType::Inventory, Record));
	TestTrue(TEXT("Commit quantity mutation"), Fixture.Projection->ApplyAuthoritativeMutationRecords(Fixture.Owner, {Record}));
	TestTrue(TEXT("Assignment survives partial consumption"), Fixture.QuickSlots->FindAssignedItem(0, Entry));
	TestEqual(TEXT("Quantity is read from projection"), Entry.GetQuantity(), 2);
	TestEqual(TEXT("Commands will use latest revision"), Entry.GetRevision(), Revision + 1);
	TestTrue(TEXT("Restore consumed tombstone"), Fixture.MakeRecord(ItemId, 0, Revision + 2,
		ERPGItemContainerType::Terminal, Record));
	TestTrue(TEXT("Commit final consumption"), Fixture.Projection->ApplyAuthoritativeMutationRecords(Fixture.Owner, {Record}));
	TestTrue(TEXT("First shortcut clears"), Fixture.QuickSlots->GetItemSlotContent(0)->IsEmpty());
	TestTrue(TEXT("Every shortcut for the removed ID clears"), Fixture.QuickSlots->GetItemSlotContent(1)->IsEmpty());
	TestFalse(TEXT("Removed item cannot be resolved"), Fixture.QuickSlots->FindAssignedItem(0, Entry));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuickSlotClientRestartBindingTest,
	"ProjectRPG.Item.QuickSlot.RebindsAfterClientRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGQuickSlotClientRestartBindingTest::RunTest(const FString& Parameters)
{
	using namespace RPGQuickSlotProjectionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("World fixture"), Fixture.World)) return false;
	ARPGPlayerController* Controller = Fixture.World->SpawnActor<ARPGPlayerController>();
	APawn* Pawn = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("Project controller"), Controller) || !TestNotNull(TEXT("Pawn"), Pawn)) return false;
	Controller->Possess(Pawn);
	URPGInventoryProjectionComponent* Projection = Controller->FindComponentByClass<URPGInventoryProjectionComponent>();
	if (!TestNotNull(TEXT("Controller-owned projection"), Projection)) return false;
	UQuickSlotComponent* Slots = NewObject<UQuickSlotComponent>(Pawn);
	Pawn->AddInstanceComponent(Slots);
	Slots->RegisterComponent();
	Slots->BindInventoryProjection(Projection, &Fixture.Policies);
	const FGuid ItemId = FGuid::NewGuid();
	FRPGItemRecord Record;
	if (!TestTrue(TEXT("Restore potion"), Fixture.MakeRecord(ItemId, 3, 7,
		ERPGItemContainerType::Inventory, Record)) ||
		!TestTrue(TEXT("Load owned projection"), Projection->ApplyAuthoritativeRecords(Fixture.Owner, {Record}))) return false;
	Slots->SetItemSlotById(0, ItemId);
	TestEqual(TEXT("Assignment already exists before restart"), Slots->GetItemSlotContent(0)->ItemId, ItemId);
	Slots->BindInventoryProjection(nullptr); // Model the early client binding before Pawn.Controller is ready.
	FRPGInventoryProjectionEntry Entry;
	TestFalse(TEXT("An unbound client cannot resolve the assigned ID"), Slots->FindAssignedItem(0, Entry));
	FRPGQuickSlotProjectionTestAccess::Restart(*Controller, Pawn);
	TestTrue(TEXT("The real restart callback restores the owner projection binding"), Slots->FindAssignedItem(0, Entry));
	TestEqual(TEXT("Binding resolves the original quantity"), Entry.GetQuantity(), 3);
	TestEqual(TEXT("Binding preserves revision"), Entry.GetRevision(), int64(7));
	FRPGQuickSlotProjectionTestAccess::Restart(*Controller, Pawn);
	TestTrue(TEXT("Repeated restart remains bound"), Slots->FindAssignedItem(0, Entry));
	TestEqual(TEXT("Restart never registers a different item"), Slots->GetItemSlotContent(0)->ItemId, ItemId);
	Slots->BindInventoryProjection(nullptr);
	return true;
}

#endif
