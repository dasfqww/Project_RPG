#if WITH_DEV_AUTOMATION_TESTS

#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Equipment/RPGEquipmentComponent.h"
#include "Component/Equipment/RPGEquipComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/RPGItemBase.h"
#include "Misc/AutomationTest.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UObject/UnrealType.h"

namespace RPGLegacyEquipmentRetirementTests
{
struct FFixture
{
	UWorld* World = nullptr;
	ACharacter* Avatar = nullptr;
	APlayerController* Controller = nullptr;
	URPGAuthoritativeEquipmentComponent* Native = nullptr;
	URPGEquipComponent* Equip = nullptr;
	URPGEquipmentComponent* Equipment = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	FRPGItemOwnerRef Owner;
	FRPGItemRecord BagItem;
	FRPGItemRecord EquippedItem;

	FRPGItemRecord MakeRecord(const ERPGItemContainerType Container, const int32 Slot) const
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 19, 1, {}, {}, State);
		FRPGItemLocation Location;
		Location.ContainerType = Container;
		Location.ContainerId = Owner.OwnerId;
		Location.SlotIndex = Slot;
		FRPGItemRecord Record;
		FRPGItemRecord::TryRestore(FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("EquipmentFacade")),
			1, Owner, Location, State, 9007199254740993LL, ERPGItemLifecycleState::Active, {}, Record);
		return Record;
	}

	FFixture()
	{
		Owner.Type = ERPGItemOwnerType::Character;
		Owner.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Avatar = World->SpawnActor<ACharacter>();
		Controller = World->SpawnActor<APlayerController>();
		if (!Avatar || !Controller) return;
		Controller->Possess(Avatar);
		Native = NewObject<URPGAuthoritativeEquipmentComponent>(Avatar);
		Avatar->AddInstanceComponent(Native);
		Native->RegisterComponent();
		Equip = NewObject<URPGEquipComponent>(Avatar);
		Avatar->AddInstanceComponent(Equip);
		Equip->RegisterComponent();
		Equipment = NewObject<URPGEquipmentComponent>(Avatar);
		Avatar->AddInstanceComponent(Equipment);
		Equipment->RegisterComponent();
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection);
		Projection->RegisterComponent();
		URPGItemCommandComponent* Commands = NewObject<URPGItemCommandComponent>(Controller);
		Controller->AddInstanceComponent(Commands);
		Commands->RegisterComponent();
		Model = NewObject<URPGInventoryProjectionViewModel>(Controller);
		Model->InitializeFromPlayerController(Controller);
		BagItem = MakeRecord(ERPGItemContainerType::Inventory, 0);
		EquippedItem = MakeRecord(ERPGItemContainerType::Equipment, static_cast<int32>(EEquipmentSlotType::Head));
		Projection->ApplyAuthoritativeRecords(Owner, { BagItem, EquippedItem });
	}

	~FFixture()
	{
		if (Model) Model->InitializeFromPlayerController(nullptr);
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}

	URPGItemBase* MakeAlias(const FGuid& Id) const
	{
		URPGItemBase* Item = NewObject<URPGItemBase>(Avatar);
		Item->SetInstanceId(Id);
		Item->SetTotalQuantity(999);
		Item->SetSlotIndex(25);
		return Item;
	}

	FRPGEquipmentSnapshot MakeSnapshot(const FRPGItemRecord& Record) const
	{
		// Simulates a validated replicated read model, not an item grant or Reconcile.
		FRPGEquipmentSnapshot Snapshot;
		Snapshot.ItemId = Record.GetItemId();
		Snapshot.DefinitionId = Record.GetDefinitionId();
		Snapshot.DefinitionVersion = Record.GetDefinitionVersion();
		Snapshot.Revision = Record.GetRevision();
		Snapshot.SlotType = static_cast<EEquipmentSlotType>(Record.GetLocation().SlotIndex);
		return Snapshot;
	}

	bool RecordUnchanged(const FRPGItemRecord& Before) const
	{
		const IRPGItemRecordSource* Records = Projection->GetAuthoritativeRecordSource();
		FRPGItemRecord After;
		return Records && Records->Find(Before.GetItemId(), After)
			&& FRPGItemRecord::StaticStruct()->CompareScriptStruct(&Before, &After, 0);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLegacyEquipmentReadThroughTest,
	"ProjectRPG.Item.LegacyEquipment.NoSecondaryStateOrRawEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGLegacyEquipmentReadThroughTest::RunTest(const FString&)
{
	using namespace RPGLegacyEquipmentRetirementTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Equipment facade fixture"), Fixture.Equipment)) return false;
	Fixture.Native->EquipmentSnapshots = { Fixture.MakeSnapshot(Fixture.EquippedItem) };
	for (const FName Name : { FName(TEXT("EquipList")), FName(TEXT("CurrentEquipState")) })
	{
		TestNull(TEXT("No second equip state property"), FindFProperty<FProperty>(URPGEquipComponent::StaticClass(), Name));
	}
	TestNull(TEXT("No second equipment list property"),
		FindFProperty<FProperty>(URPGEquipmentComponent::StaticClass(), TEXT("EquipmentList")));
	TestFalse(TEXT("Equip facade does not replicate item subobjects"), Fixture.Equip->GetIsReplicated());
	TestFalse(TEXT("Equipment facade does not replicate item subobjects"), Fixture.Equipment->GetIsReplicated());
	int32 EquipChanges = 0;
	int32 EquipmentChanges = 0;
	Fixture.Equip->OnEquipItemChanged.AddLambda([&](EEquipmentSlotType, URPGItemBase*, int32) { ++EquipChanges; });
	Fixture.Equipment->OnEquipmentChanged.AddLambda([&](EEquipmentSlotType, URPGItemBase*, int32) { ++EquipmentChanges; });
	URPGItemBase* Alias = Fixture.MakeAlias(Fixture.EquippedItem.GetItemId());
	TestEqual(TEXT("Legacy quantity is not a validation policy"),
		Fixture.Equipment->CanAddEquipment(Alias, EEquipmentSlotType::Head), 0);
	Fixture.Equipment->AddEquipment_Internal(EEquipmentSlotType::Head, Alias, 999);
	TestNull(TEXT("Raw remove cannot bypass a commit"), Fixture.Equipment->RemoveEquipment_Internal(EEquipmentSlotType::Head));
	TestTrue(TEXT("No second item list is reconstructed"), Fixture.Equipment->GetAllEntries().IsEmpty());
	TestNull(TEXT("Equipment getter has no mutable mirror"), Fixture.Equipment->GetItemInSlot(EEquipmentSlotType::Head));
	TestNull(TEXT("Equip getter has no mutable mirror"), Fixture.Equip->GetItemInSlot(EEquipmentSlotType::Head));
	TestEqual(TEXT("Count is read from native equipment presence"), Fixture.Equipment->GetItemCountInSlot(EEquipmentSlotType::Head), 1);
	TestEqual(TEXT("Empty native slot has no count"), Fixture.Equipment->GetItemCountInSlot(EEquipmentSlotType::Chest), 0);
	TestEqual(TEXT("No legacy item event claims an uncommitted change"), EquipChanges + EquipmentChanges, 0);
	TestEqual(TEXT("Legacy quantity preserved"), Alias->GetTotalQuantity(), 999);
	TestTrue(TEXT("Raw calls preserve native records"), Fixture.RecordUnchanged(Fixture.EquippedItem));
	TestEqual(TEXT("Raw calls apply no equipment resources"), Fixture.Native->NumAppliedItems(), 0);

	FRPGEquipmentSnapshot Weapon = Fixture.MakeSnapshot(Fixture.MakeRecord(
		ERPGItemContainerType::Equipment, static_cast<int32>(EEquipmentSlotType::Weapon_Primary_L)));
	Weapon.WeaponHandType = EWeaponHandType::LeftHand;
	Weapon.EquippedActor = Fixture.World->SpawnActor<AActor>();
	Fixture.Native->EquipmentSnapshots.Add(Weapon);
	TestEqual(TEXT("Actor getter reads native snapshot"),
		Fixture.Equip->GetSpawnedActorInSlot(Weapon.SlotType), Weapon.EquippedActor.Get());
	URPGItemBase* Item = Alias;
	AActor* Actor = nullptr;
	EEquipmentSlotType Slot = EEquipmentSlotType::None;
	TestTrue(TEXT("Weapon compatibility getter reads native state"),
		Fixture.Equip->FindEquippedWeapon(EWeaponHandType::LeftHand, Item, Actor, Slot));
	TestNull(TEXT("Weapon read does not materialize legacy item"), Item);
	TestEqual(TEXT("Native actor retained"), Actor, Weapon.EquippedActor.Get());
	Fixture.Native->EquipmentSnapshots.Last().EquippedActor = nullptr;
	TestTrue(TEXT("Loading weapon presence is retained without fallback"),
		Fixture.Equip->FindEquippedWeapon(EWeaponHandType::LeftHand, Item, Actor, Slot));
	TestNull(TEXT("Loading actor stays null"), Actor);
	Fixture.Native->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestFalse(TEXT("Inactive set is not borrowed"),
		Fixture.Equip->FindEquippedWeapon(EWeaponHandType::LeftHand, Item, Actor, Slot));
	TestEqual(TEXT("Rejected read clears slot"), Slot, EEquipmentSlotType::None);
	Fixture.Native->EquipmentSnapshots.Reset();
	TestEqual(TEXT("Removed native slot no longer appears equipped"),
		Fixture.Equipment->GetItemCountInSlot(EEquipmentSlotType::Head), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLegacyEquipmentCommandRoutingTest,
	"ProjectRPG.Item.LegacyEquipment.CanonicalCommandsKeepServerGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGLegacyEquipmentCommandRoutingTest::RunTest(const FString&)
{
	using namespace RPGLegacyEquipmentRetirementTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Equipment facade fixture"), Fixture.Equip)) return false;
	Fixture.Native->EquipmentSnapshots = { Fixture.MakeSnapshot(Fixture.EquippedItem) };
	Fixture.Equip->EquipItem(EEquipmentSlotType::Head, nullptr);
	Fixture.Equip->EquipItem(EEquipmentSlotType::Head, Fixture.MakeAlias(FGuid::NewGuid()));
	Fixture.Equip->EquipItem(EEquipmentSlotType::Head, Fixture.MakeAlias(Fixture.EquippedItem.GetItemId()));
	Fixture.Equip->UnequipItem(EEquipmentSlotType::Chest);
	TestFalse(TEXT("Unknown IDs and missing slots submit no command"), Fixture.Model->bHasLastCommandResult);
	URPGItemBase* Alias = Fixture.MakeAlias(Fixture.BagItem.GetItemId());
	Fixture.Equip->EquipItem(EEquipmentSlotType::None, Alias);
	TestFalse(TEXT("Invalid slot submits no command"), Fixture.Model->bHasLastCommandResult);
	Fixture.Equip->EquipItem(EEquipmentSlotType::Head, Alias);
	TestTrue(TEXT("Existing canonical bag ID forwards a native command"), Fixture.Model->bHasLastCommandResult);
	TestEqual(TEXT("Native equip operation"), Fixture.Model->LastCommandResult.Operation, FName(TEXT("Item.Equip")));
	// This fixture has no admitted/authenticated RPG PlayerState or backend. The native
	// command must reject; forwarding is not a successful equip or an authorization bypass.
	TestEqual(TEXT("Native equip security/backend gates remain"), Fixture.Model->LastCommandResult.Result,
		ERPGItemCommandResultCode::ServerStateError);
	TestTrue(TEXT("Rejected equip does not replace occupied native slot"), Fixture.RecordUnchanged(Fixture.EquippedItem));
	TestTrue(TEXT("Rejected equip does not remove bag item"), Fixture.RecordUnchanged(Fixture.BagItem));
	Fixture.Model->bHasLastCommandResult = false;
	Fixture.Equip->UnequipItem(EEquipmentSlotType::Head);
	TestTrue(TEXT("Known native equipment forwards unequip"), Fixture.Model->bHasLastCommandResult);
	TestEqual(TEXT("Native unequip operation"), Fixture.Model->LastCommandResult.Operation, FName(TEXT("Item.Unequip")));
	TestEqual(TEXT("Native unequip gates remain"), Fixture.Model->LastCommandResult.Result,
		ERPGItemCommandResultCode::ServerStateError);
	TestTrue(TEXT("Rejected unequip keeps equipment record"), Fixture.RecordUnchanged(Fixture.EquippedItem));
	TestEqual(TEXT("Legacy quantity never used or debited"), Alias->GetTotalQuantity(), 999);

	TArray<FRPGItemRecord> FullBag = { Fixture.EquippedItem };
	for (int32 Index = 0; Index < RPGItemInventoryPolicy::SlotCapacity; ++Index)
	{
		FullBag.Add(Fixture.MakeRecord(ERPGItemContainerType::Inventory, Index));
	}
	TestTrue(TEXT("Prepare full native bag"), Fixture.Projection->ApplyAuthoritativeRecords(Fixture.Owner, FullBag));
	Fixture.Model->bHasLastCommandResult = false;
	Fixture.Equip->UnequipItem(EEquipmentSlotType::Head);
	TestFalse(TEXT("Full bag cannot submit an unequip without a destination"), Fixture.Model->bHasLastCommandResult);
	TestTrue(TEXT("Full bag preserves equipped record"), Fixture.RecordUnchanged(Fixture.EquippedItem));
	TestTrue(TEXT("Restore current records"), Fixture.Projection->ApplyAuthoritativeRecords(
		Fixture.Owner, { Fixture.BagItem, Fixture.EquippedItem }));
	Fixture.Controller->UnPossess();
	Fixture.Equip->EquipItem(EEquipmentSlotType::Head, Alias);
	Fixture.Equip->UnequipItem(EEquipmentSlotType::Head);
	TestFalse(TEXT("Unpossessed old pawn cannot submit commands for the controller"), Fixture.Model->bHasLastCommandResult);
	TestTrue(TEXT("Detached facade preserves records"), Fixture.RecordUnchanged(Fixture.EquippedItem));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLegacyEquipmentStateBridgeTest,
	"ProjectRPG.Item.LegacyEquipment.SingleNativeStateAndNotifications",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGLegacyEquipmentStateBridgeTest::RunTest(const FString&)
{
	using namespace RPGLegacyEquipmentRetirementTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Equipment facade fixture"), Fixture.Equip)) return false;
	int32 Notifications = 0;
	EEquipState Previous = EEquipState::Count;
	EEquipState Current = EEquipState::Count;
	Fixture.Equip->OnEquipStateChanged.AddLambda([&](const EEquipState Old, const EEquipState New)
		{ ++Notifications; Previous = Old; Current = New; });
	TestEqual(TEXT("Facade reads native default, not a second None field"),
		Fixture.Equip->GetCurrentEquipState(), EEquipState::WeaponSet_Primary);
	Fixture.Equip->ChangeEquipState(EEquipState::None);
	TestEqual(TEXT("Wrapper changes native state"), Fixture.Native->GetActiveEquipState(), EEquipState::None);
	TestEqual(TEXT("Wrapper produces a single native notification"), Notifications, 1);
	TestEqual(TEXT("Notification previous state"), Previous, EEquipState::WeaponSet_Primary);
	TestEqual(TEXT("Notification new state"), Current, EEquipState::None);
	Fixture.Equip->ChangeEquipState(EEquipState::None);
	Fixture.Equip->ChangeEquipState(static_cast<EEquipState>(255));
	TestEqual(TEXT("Same or invalid state does not duplicate events"), Notifications, 1);
	Fixture.Native->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestEqual(TEXT("Direct native change reaches compatibility listeners"), Notifications, 2);
	TestEqual(TEXT("Facade reads direct native change"), Fixture.Equip->GetCurrentEquipState(), EEquipState::WeaponSet_Secondary);
	// Exercise the owner-client notification callback without claiming a real network test.
	Fixture.Native->ActiveEquipState = EEquipState::Utility;
	Fixture.Native->OnRep_ActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestEqual(TEXT("RepNotify bridges owner-client state notification"), Notifications, 3);
	Fixture.Native->OnRep_ActiveEquipState(EEquipState::Utility);
	TestEqual(TEXT("No unchanged RepNotify event"), Notifications, 3);
	Fixture.Equip->UnregisterComponent();
	Fixture.Native->SetActiveEquipState(EEquipState::None);
	TestEqual(TEXT("Unregister detaches native delegate"), Notifications, 3);
	Fixture.Equip->RegisterComponent();
	Fixture.Native->SetActiveEquipState(EEquipState::WeaponSet_Primary);
	TestEqual(TEXT("Reregister binds once"), Notifications, 4);
	Fixture.Avatar->SetRole(ROLE_SimulatedProxy);
	Fixture.Equip->ChangeEquipState(EEquipState::None);
	Fixture.Equip->EquipItem(EEquipmentSlotType::Head, Fixture.MakeAlias(Fixture.BagItem.GetItemId()));
	Fixture.Equip->UnequipItem(EEquipmentSlotType::Head);
	TestEqual(TEXT("Client-side facade cannot change authoritative active state"),
		Fixture.Native->GetActiveEquipState(), EEquipState::WeaponSet_Primary);
	TestEqual(TEXT("Client write attempt produces no state event"), Notifications, 4);
	TestFalse(TEXT("Client-side facade submits no server-local command"), Fixture.Model->bHasLastCommandResult);
	Fixture.Avatar->SetRole(ROLE_Authority);
	return true;
}

#endif
