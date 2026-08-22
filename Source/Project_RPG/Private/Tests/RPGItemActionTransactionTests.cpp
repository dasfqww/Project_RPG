#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Persistence/RPGInMemoryItemRepository.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Transaction/RPGItemTransactionService.h"
#include "GameplayEffect.h"

namespace RPGItemActionTransactionTests
{
URPGItemDefinition* MakeEquipmentDefinition(const TCHAR* BaseName)
{
	URPGItemDefinition* Definition = NewObject<URPGItemDefinition>(
		GetTransientPackage(),
		MakeUniqueObjectName(
			GetTransientPackage(),
			URPGItemDefinition::StaticClass(),
			FName(BaseName)));
	Definition->DefinitionVersion = 1;
	Definition->ItemCategory = EItemCategory::Equip;
	Definition->MaxStackSize = 1;

	URPGItemEquipmentDefinitionFragment* Equipment =
		NewObject<URPGItemEquipmentDefinitionFragment>(Definition);
	Equipment->CompatibleSlots.Add(
		EEquipmentSlotType::Weapon_Primary_R);
	Definition->Fragments.Add(Equipment);
	return Definition;
}

URPGItemDefinition* MakeConsumableDefinition(
	const TCHAR* BaseName,
	const int32 QuantityPerUse)
{
	URPGItemDefinition* Definition = NewObject<URPGItemDefinition>(
		GetTransientPackage(),
		MakeUniqueObjectName(
			GetTransientPackage(),
			URPGItemDefinition::StaticClass(),
			FName(BaseName)));
	Definition->DefinitionVersion = 1;
	Definition->ItemCategory = EItemCategory::Consume;
	Definition->MaxStackSize = 10;

	URPGItemConsumableDefinitionFragment* Consumable =
		NewObject<URPGItemConsumableDefinitionFragment>(Definition);
	Consumable->QuantityPerUse = QuantityPerUse;
	Consumable->GameplayEffect = UGameplayEffect::StaticClass();
	Definition->Fragments.Add(Consumable);
	return Definition;
}

URPGItemDefinition* MakePassiveDefinition(const TCHAR* BaseName)
{
	URPGItemDefinition* Definition = NewObject<URPGItemDefinition>(
		GetTransientPackage(),
		MakeUniqueObjectName(
			GetTransientPackage(),
			URPGItemDefinition::StaticClass(),
			FName(BaseName)));
	Definition->DefinitionVersion = 1;
	Definition->ItemCategory = EItemCategory::Craft;
	Definition->MaxStackSize = 10;
	return Definition;
}

FRPGItemOwnerRef MakeOwner()
{
	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = TEXT("character:item-action-test");
	return Owner;
}

FRPGItemLocation MakeInventoryLocation(const int32 SlotIndex)
{
	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Inventory;
	Location.ContainerId = TEXT("inventory:main");
	Location.SlotIndex = SlotIndex;
	return Location;
}

bool MakeRecord(
	const URPGItemDefinition& Definition,
	const FRPGItemOwnerRef& Owner,
	const int32 SlotIndex,
	const int32 Quantity,
	const int32 Seed,
	const ERPGItemBindState BindState,
	FRPGItemRecord& OutRecord)
{
	FRPGItemInstanceState State;
	Definition.BuildInstanceState(Quantity, Seed, State);
	FRPGItemRecordMetadata Metadata;
	Metadata.BindState = BindState;
	return FRPGItemRecord::TryCreate(
		Definition.GetPrimaryAssetId(),
		Definition.GetDefinitionVersion(),
		Owner,
		MakeInventoryLocation(SlotIndex),
		State,
		Metadata,
		OutRecord);
}

bool SeedRecord(
	FRPGInMemoryItemRepository& Repository,
	const FRPGItemRecord& Record)
{
	FRPGItemRepositoryCommitRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Operation = TEXT("Test.SeedActionItem");
	Request.CommandFingerprint = Record.GetItemId().ToString(
		EGuidFormats::DigitsWithHyphens);
	Request.Actor = Record.GetOwner();
	Request.AffectedQuantity = Record.GetQuantity();
	FRPGItemRecordMutation& Mutation =
		Request.Mutations.AddDefaulted_GetRef();
	Mutation.NewRecord = Record;
	return Repository.Commit(Request).WasApplied();
}

bool RegisterDefinition(
	const URPGItemDefinition& Definition,
	FRPGItemDefinitionRegistry& Definitions,
	FRPGItemActionPolicyRegistry& Actions)
{
	return Definitions.RegisterDefinition(Definition) &&
		Actions.RegisterDefinition(Definition);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemEquipUnequipTransactionTest,
	"ProjectRPG.Item.Transaction.EquipUnequipIsValidatedAndIdempotent",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemEquipUnequipTransactionTest::RunTest(
	const FString& Parameters)
{
	using namespace RPGItemActionTransactionTests;

	URPGItemDefinition* Definition =
		MakeEquipmentDefinition(TEXT("EquipActionDefinition"));
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	TestTrue(
		TEXT("Equipment definition and action policy register"),
		RegisterDefinition(*Definition, Definitions, Actions));

	FRPGInMemoryItemRepository Repository;
	const FRPGItemOwnerRef Owner = MakeOwner();
	FRPGItemRecord FirstItem;
	FRPGItemRecord SecondItem;
	TestTrue(
		TEXT("Bind-on-equip item is created"),
		MakeRecord(
			*Definition,
			Owner,
			0,
			1,
			100,
			ERPGItemBindState::BindOnEquip,
			FirstItem));
	TestTrue(
		TEXT("Second equipment item is created"),
		MakeRecord(
			*Definition,
			Owner,
			1,
			1,
			200,
			ERPGItemBindState::Unbound,
			SecondItem));
	TestTrue(TEXT("First equipment item is seeded"),
		SeedRecord(Repository, FirstItem));
	TestTrue(TEXT("Second equipment item is seeded"),
		SeedRecord(Repository, SecondItem));

	FRPGItemTransactionService Service(
		Repository,
		Definitions,
		Actions);

	FRPGItemMoveRequest GenericEquipMove;
	GenericEquipMove.RequestId = FGuid::NewGuid();
	GenericEquipMove.Actor = Owner;
	GenericEquipMove.ItemId = FirstItem.GetItemId();
	GenericEquipMove.ExpectedRevision = 1;
	GenericEquipMove.Destination.ContainerType =
		ERPGItemContainerType::Equipment;
	GenericEquipMove.Destination.ContainerId = TEXT("equipment:main");
	GenericEquipMove.Destination.SlotIndex = static_cast<int32>(
		EEquipmentSlotType::Weapon_Primary_R);
	TestTrue(
		TEXT("Generic move cannot bypass equip policy"),
		Service.MoveItem(GenericEquipMove).Status ==
			ERPGItemTransactionStatus::InvalidContainerTransition);

	FRPGItemEquipRequest WrongSlot;
	WrongSlot.RequestId = FGuid::NewGuid();
	WrongSlot.Actor = Owner;
	WrongSlot.ItemId = FirstItem.GetItemId();
	WrongSlot.ExpectedRevision = 1;
	WrongSlot.EquipmentContainerId = TEXT("equipment:main");
	WrongSlot.SlotType = EEquipmentSlotType::Head;
	TestTrue(
		TEXT("An incompatible equipment slot is rejected"),
		Service.EquipItem(WrongSlot).Status ==
			ERPGItemTransactionStatus::IncompatibleEquipmentSlot);

	FRPGItemEquipRequest EquipRequest = WrongSlot;
	EquipRequest.RequestId = FGuid::NewGuid();
	EquipRequest.SlotType = EEquipmentSlotType::Weapon_Primary_R;
	const FRPGItemTransactionResult Equipped =
		Service.EquipItem(EquipRequest);
	TestTrue(
		TEXT("Compatible equipment is committed"),
		Equipped.Status == ERPGItemTransactionStatus::Succeeded);
	const FRPGItemRecord* EquippedRecord =
		Equipped.FindRecord(FirstItem.GetItemId());
	TestNotNull(TEXT("Equip returns the authoritative record"), EquippedRecord);
	if (EquippedRecord)
	{
		TestTrue(
			TEXT("The equipment container is persisted"),
			EquippedRecord->GetLocation().ContainerType ==
				ERPGItemContainerType::Equipment);
		TestEqual(
			TEXT("The equipment slot is persisted"),
			EquippedRecord->GetLocation().SlotIndex,
			static_cast<int32>(EEquipmentSlotType::Weapon_Primary_R));
		TestEqual(
			TEXT("Equip increments the revision"),
			EquippedRecord->GetRevision(),
			static_cast<int64>(2));
		TestTrue(
			TEXT("Bind-on-equip becomes character-bound atomically"),
			EquippedRecord->GetMetadata().BindState ==
				ERPGItemBindState::CharacterBound);
	}

	TestTrue(
		TEXT("An equip retry returns the original receipt"),
		Service.EquipItem(EquipRequest).Status ==
			ERPGItemTransactionStatus::AlreadyApplied);

	FRPGItemEquipRequest OccupiedSlot = EquipRequest;
	OccupiedSlot.RequestId = FGuid::NewGuid();
	OccupiedSlot.ItemId = SecondItem.GetItemId();
	TestTrue(
		TEXT("Another item cannot occupy the equipped slot"),
		Service.EquipItem(OccupiedSlot).Status ==
			ERPGItemTransactionStatus::LocationOccupied);

	FRPGItemUnequipRequest UnequipRequest;
	UnequipRequest.RequestId = FGuid::NewGuid();
	UnequipRequest.Actor = Owner;
	UnequipRequest.ItemId = FirstItem.GetItemId();
	UnequipRequest.ExpectedRevision = 2;
	UnequipRequest.InventoryDestination = MakeInventoryLocation(4);
	const FRPGItemTransactionResult Unequipped =
		Service.UnequipItem(UnequipRequest);
	TestTrue(
		TEXT("Equipped item returns to inventory"),
		Unequipped.Status == ERPGItemTransactionStatus::Succeeded);
	const FRPGItemRecord* UnequippedRecord =
		Unequipped.FindRecord(FirstItem.GetItemId());
	if (UnequippedRecord)
	{
		TestEqual(
			TEXT("Unequip increments the revision"),
			UnequippedRecord->GetRevision(),
			static_cast<int64>(3));
		TestTrue(
			TEXT("Unequip never removes a permanent bind"),
			UnequippedRecord->GetMetadata().BindState ==
				ERPGItemBindState::CharacterBound);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemConsumeTransactionTest,
	"ProjectRPG.Item.Transaction.ConsumeIsAtomicAndIdempotent",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemConsumeTransactionTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemActionTransactionTests;

	URPGItemDefinition* Consumable =
		MakeConsumableDefinition(TEXT("ConsumeActionDefinition"), 2);
	URPGItemDefinition* Passive =
		MakePassiveDefinition(TEXT("PassiveActionDefinition"));
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	TestTrue(TEXT("Consumable policy registers"),
		RegisterDefinition(*Consumable, Definitions, Actions));
	TestTrue(TEXT("A definition with no item action still registers"),
		RegisterDefinition(*Passive, Definitions, Actions));

	FRPGInMemoryItemRepository Repository;
	const FRPGItemOwnerRef Owner = MakeOwner();
	FRPGItemRecord Stack;
	FRPGItemRecord InsufficientStack;
	FRPGItemRecord PassiveStack;
	MakeRecord(
		*Consumable,
		Owner,
		0,
		4,
		300,
		ERPGItemBindState::Unbound,
		Stack);
	MakeRecord(
		*Consumable,
		Owner,
		1,
		1,
		301,
		ERPGItemBindState::Unbound,
		InsufficientStack);
	MakeRecord(
		*Passive,
		Owner,
		2,
		1,
		302,
		ERPGItemBindState::Unbound,
		PassiveStack);
	TestTrue(TEXT("Consumable stack is seeded"),
		SeedRecord(Repository, Stack));
	TestTrue(TEXT("Small consumable stack is seeded"),
		SeedRecord(Repository, InsufficientStack));
	TestTrue(TEXT("Passive stack is seeded"),
		SeedRecord(Repository, PassiveStack));

	FRPGItemTransactionService Service(
		Repository,
		Definitions,
		Actions);
	FRPGItemConsumeRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Actor = Owner;
	Request.ItemId = Stack.GetItemId();
	Request.ExpectedRevision = 1;
	const FRPGItemTransactionResult FirstUse =
		Service.ConsumeItem(Request);
	TestTrue(
		TEXT("First use commits"),
		FirstUse.Status == ERPGItemTransactionStatus::Succeeded);
	TestEqual(TEXT("The authored quantity-per-use is reported"),
		FirstUse.AffectedQuantity, 2);
	const FRPGItemRecord* FirstRecord =
		FirstUse.FindRecord(Stack.GetItemId());
	if (FirstRecord)
	{
		TestEqual(TEXT("First use decrements exactly once"),
			FirstRecord->GetQuantity(), 2);
		TestEqual(TEXT("First use increments the revision"),
			FirstRecord->GetRevision(), static_cast<int64>(2));
	}

	TestTrue(
		TEXT("A retry returns the cached receipt"),
		Service.ConsumeItem(Request).Status ==
			ERPGItemTransactionStatus::AlreadyApplied);
	FRPGItemRecord StoredAfterRetry;
	Repository.Find(Stack.GetItemId(), StoredAfterRetry);
	TestEqual(TEXT("A retry does not consume twice"),
		StoredAfterRetry.GetQuantity(), 2);

	FRPGItemConsumeRequest FinalUse = Request;
	FinalUse.RequestId = FGuid::NewGuid();
	FinalUse.ExpectedRevision = 2;
	const FRPGItemTransactionResult FinalResult =
		Service.ConsumeItem(FinalUse);
	TestTrue(
		TEXT("Final use commits"),
		FinalResult.Status == ERPGItemTransactionStatus::Succeeded);
	const FRPGItemRecord* TerminalRecord =
		FinalResult.FindRecord(Stack.GetItemId());
	if (TerminalRecord)
	{
		TestEqual(TEXT("Final use produces zero quantity"),
			TerminalRecord->GetQuantity(), 0);
		TestTrue(
			TEXT("Final use persists a consumed tombstone"),
			TerminalRecord->GetLifecycleState() ==
				ERPGItemLifecycleState::Consumed &&
			TerminalRecord->GetLocation().IsTerminalLocation());
		TestEqual(TEXT("Final use increments the revision"),
			TerminalRecord->GetRevision(), static_cast<int64>(3));
	}

	FRPGItemConsumeRequest InsufficientRequest;
	InsufficientRequest.RequestId = FGuid::NewGuid();
	InsufficientRequest.Actor = Owner;
	InsufficientRequest.ItemId = InsufficientStack.GetItemId();
	InsufficientRequest.ExpectedRevision = 1;
	TestTrue(
		TEXT("A partial authored use is rejected"),
		Service.ConsumeItem(InsufficientRequest).Status ==
			ERPGItemTransactionStatus::InsufficientQuantity);

	FRPGItemConsumeRequest PassiveRequest = InsufficientRequest;
	PassiveRequest.RequestId = FGuid::NewGuid();
	PassiveRequest.ItemId = PassiveStack.GetItemId();
	TestTrue(
		TEXT("A passive item cannot be consumed"),
		Service.ConsumeItem(PassiveRequest).Status ==
			ERPGItemTransactionStatus::NotConsumable);
	return true;
}

#endif
