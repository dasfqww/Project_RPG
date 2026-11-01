#if WITH_DEV_AUTOMATION_TESTS

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Misc/AutomationTest.h"
#include "RPGItemTags.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

namespace RPGItemProjectionInteractionTests
{
struct FFixture
{
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	FRPGItemActionPolicyRegistry Policies;
	FRPGItemOwnerRef Owner;
	FPrimaryAssetId EquipmentId{ TEXT("RPGItemDefinition"), TEXT("InteractionEquipment") };
	FPrimaryAssetId ConsumableId{ TEXT("RPGItemDefinition"), TEXT("InteractionPotion") };

	FFixture()
	{
		Owner.Type = ERPGItemOwnerType::Character;
		Owner.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>();
		if (!Controller) return;
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection);
		Projection->RegisterComponent();
		URPGItemCommandComponent* Commands = NewObject<URPGItemCommandComponent>(Controller);
		Controller->AddInstanceComponent(Commands);
		Commands->RegisterComponent();
		Model = NewObject<URPGInventoryProjectionViewModel>(Controller);
		Model->InitializeFromPlayerController(Controller);
		FRPGItemActionPolicy Equipment;
		Equipment.DefinitionId = EquipmentId;
		Equipment.DefinitionVersion = 1;
		Equipment.CompatibleEquipmentSlots.Add(EEquipmentSlotType::Head);
		Policies.RegisterPolicy(Equipment);
		FRPGItemActionPolicy Consumable;
		Consumable.DefinitionId = ConsumableId;
		Consumable.DefinitionVersion = 1;
		Consumable.QuantityPerUse = 1;
		Policies.RegisterPolicy(Consumable);
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
	FRPGItemRecord MakeRecord(const FPrimaryAssetId& Definition, ERPGItemContainerType Container,
		int32 Slot, int32 Quantity = 1, const FRPGEquipmentProgressionState& Growth = {}) const
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 31, Quantity, {}, {}, State, Growth);
		FRPGItemLocation Location;
		Location.ContainerType = Container;
		Location.ContainerId = Owner.OwnerId;
		Location.SlotIndex = Slot;
		FRPGItemRecord Record;
		FRPGItemRecord::TryRestore(Definition, 1, Owner, Location, State,
			9007199254740993LL, ERPGItemLifecycleState::Active, {}, Record);
		return Record;
	}
	bool Load(const TArray<FRPGItemRecord>& Records)
	{
		if (!Projection || !Model || !Projection->ApplyAuthoritativeRecords(Owner, Records)) return false;
		// A real load publishes Ready on the component too. Setting only the ViewModel
		// leaves authoritative-revision reconciliation deliberately blocked as uninitialized.
		auto* StateProperty = FindFProperty<FEnumProperty>(Projection->GetClass(), TEXT("LoadState"));
		if (!StateProperty || !StateProperty->GetUnderlyingProperty()) return false;
		StateProperty->GetUnderlyingProperty()->SetIntPropertyValue(
			StateProperty->ContainerPtrToValuePtr<void>(Projection),
			static_cast<uint64>(ERPGInventoryProjectionLoadState::Ready));
		Projection->OnLoadStateChanged.Broadcast(ERPGInventoryProjectionLoadState::Ready);
		return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemDefaultInteractionTest,
	"ProjectRPG.Item.Interaction.DefaultActionUsesMatchingPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemDefaultInteractionTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("ViewModel fixture"), Fixture.Model)) return false;
	const auto Inventory = ERPGItemContainerType::Inventory;
	const auto Equipment = ERPGItemContainerType::Equipment;
	const auto Helm = Fixture.MakeRecord(Fixture.EquipmentId, Inventory, 4);
	const auto Occupant = Fixture.MakeRecord(Fixture.EquipmentId, Equipment,
		static_cast<int32>(EEquipmentSlotType::Head));
	const auto Potion = Fixture.MakeRecord(Fixture.ConsumableId, Inventory, 5, 3);
	if (!TestTrue(TEXT("Load owned entries"), Fixture.Load({ Helm, Occupant, Potion }))) return false;
	FGuid RequestId = FGuid::NewGuid();
	TestFalse(TEXT("Default equip does not replace an occupied slot"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Helm.GetItemId(), Fixture.Policies, RequestId));
	TestFalse(TEXT("Rejected action clears request identity"), RequestId.IsValid());
	TestFalse(TEXT("Occupied equipment explains the local refusal"), Fixture.Model->LocalActionFeedback.IsEmpty());
	TestFalse(TEXT("Local refusal is not a server result"), Fixture.Model->bHasLastCommandResult);
	TestEqual(TEXT("Local refusal reserves no request"), Fixture.Model->PendingItemCommandCount, 0);
	TestFalse(TEXT("Invalid enum slot is rejected"), Fixture.Model->RequestEquip(
		Helm.GetItemId(), static_cast<EEquipmentSlotType>(255), RequestId));
	TestFalse(TEXT("Equipment cannot be used as an inventory item"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Occupant.GetItemId(), Fixture.Policies, RequestId));
	TestFalse(TEXT("Unknown item cannot submit a command"),
		Fixture.Model->RequestDefaultActionUsingPolicies(FGuid::NewGuid(), Fixture.Policies, RequestId));
	Fixture.Load({ Helm, Potion });
	TestTrue(TEXT("Free compatible slot submits equip"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Helm.GetItemId(), Fixture.Policies, RequestId));
	TestTrue(TEXT("Submitted command has an ID"), RequestId.IsValid());
	TestTrue(TEXT("A submitted action clears earlier local feedback"), Fixture.Model->LocalActionFeedback.IsEmpty());
	TestEqual(TEXT("Facade receives equip, not a legacy mutation"),
		Fixture.Model->LastCommandResult.Operation, FName(TEXT("Item.Equip")));
	FRPGInventoryProjectionEntry Entry;
	TestTrue(TEXT("Item stays projected until a backend receipt"),
		Fixture.Projection->FindProjectedItem(Helm.GetItemId(), Entry));
	TestEqual(TEXT("UI does not equip optimistically"), Entry.GetContainerType(), Inventory);
	TestEqual(TEXT("UI preserves full int64 revision"), Entry.GetRevision(), Helm.GetRevision());
	TestTrue(TEXT("Consumable policy submits consume"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Potion.GetItemId(), Fixture.Policies, RequestId));
	TestEqual(TEXT("Facade receives consume"), Fixture.Model->LastCommandResult.Operation,
		FName(TEXT("Item.Consume")));
	Fixture.Model->LoadState = ERPGInventoryProjectionLoadState::Loading;
	TestFalse(TEXT("Loading blocks item commands"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Potion.GetItemId(), Fixture.Policies, RequestId));
	Fixture.Model->LoadState = ERPGInventoryProjectionLoadState::Ready;
	FRPGItemActionPolicy WrongVersion;
	WrongVersion.DefinitionId = Fixture.ConsumableId;
	WrongVersion.DefinitionVersion = 2;
	WrongVersion.QuantityPerUse = 1;
	Fixture.Policies.RegisterPolicy(WrongVersion);
	TestFalse(TEXT("Policy version must match the projected definition"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Potion.GetItemId(), Fixture.Policies, RequestId));
	const FText PolicyFeedback = Fixture.Model->LocalActionFeedback;
	TestFalse(TEXT("Out-of-date policy is explained locally"), PolicyFeedback.IsEmpty());
	WrongVersion.DefinitionVersion = 1;
	WrongVersion.QuantityPerUse = 4;
	Fixture.Policies.RegisterPolicy(WrongVersion);
	TestFalse(TEXT("Insufficient quantity does not submit consume"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Potion.GetItemId(), Fixture.Policies, RequestId));
	TestFalse(TEXT("Insufficient quantity has a distinct explanation"), Fixture.Model->LocalActionFeedback.IsEmpty() ||
		Fixture.Model->LocalActionFeedback.EqualTo(PolicyFeedback));
	WrongVersion.QuantityPerUse = 0;
	Fixture.Policies.RegisterPolicy(WrongVersion);
	TestFalse(TEXT("An item without a use or equip action is explained without a command"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Potion.GetItemId(), Fixture.Policies, RequestId));
	TestFalse(TEXT("No-action policy exposes feedback"), Fixture.Model->LocalActionFeedback.IsEmpty());
	TestFalse(TEXT("No-action refusal does not create a request identity"), RequestId.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemLocalActionFeedbackTest,
	"ProjectRPG.Item.Presentation.LocalActionFeedbackIsNotAServerReceipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemLocalActionFeedbackTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("ViewModel fixture"), Fixture.Model)) return false;
	const auto Helm = Fixture.MakeRecord(Fixture.EquipmentId, ERPGItemContainerType::Equipment,
		static_cast<int32>(EEquipmentSlotType::Head));
	TArray<FRPGItemRecord> Records{Helm};
	for (int32 Index = 0; Index < RPGItemInventoryPolicy::SlotCapacity; ++Index)
		Records.Add(Fixture.MakeRecord(Fixture.ConsumableId, ERPGItemContainerType::Inventory, Index));
	if (!TestTrue(TEXT("Full bag projected"), Fixture.Load(Records))) return false;
	FRPGItemCommandClientResult Previous;
	Previous.RequestId = FGuid::NewGuid(); Previous.Operation = TEXT("Item.Consume");
	Previous.Result = ERPGItemCommandResultCode::Succeeded;
	Fixture.Model->LastCommandResult = Previous;
	Fixture.Model->bHasLastCommandResult = true;
	FGuid RequestId = FGuid::NewGuid();
	TestFalse(TEXT("Full bag refuses unequip locally"), Fixture.Model->RequestUnequipToFirstFreeSlot(Helm.GetItemId(), RequestId));
	const FText FullFeedback = Fixture.Model->LocalActionFeedback;
	TestFalse(TEXT("Full bag refusal has a reason"), FullFeedback.IsEmpty());
	TestTrue(TEXT("Local refusal supersedes an old success in the footer"), Fixture.Model->GetCommandFeedbackText().EqualTo(FullFeedback));
	TestEqual(TEXT("Local refusal preserves the actual server request identity"), Fixture.Model->LastCommandResult.RequestId, Previous.RequestId);
	TestEqual(TEXT("Local refusal preserves the actual server result"), Fixture.Model->LastCommandResult.Result, Previous.Result);
	TestFalse(TEXT("Local refusal creates no request identity"), RequestId.IsValid());
	TestEqual(TEXT("Local refusal creates no reservation"), Fixture.Model->PendingItemCommandCount, 0);

	Records.RemoveAt(Records.Num() - 1);
	if (!TestTrue(TEXT("One bag slot freed by the authoritative snapshot"), Fixture.Load(Records))) return false;
	TestTrue(TEXT("A new snapshot clears obsolete local feedback"), Fixture.Model->LocalActionFeedback.IsEmpty());
	const FGuid PendingId = FGuid::NewGuid();
	URPGInventoryProjectionViewModel::FPendingInventoryMove Pending;
	Pending.ItemId = Records[1].GetItemId(); Pending.ExpectedRevision = Records[1].GetRevision();
	Pending.DestinationSlot = RPGItemInventoryPolicy::SlotCapacity - 1;
	Fixture.Model->PendingInventoryMovesByRequest.Add(PendingId, Pending);
	Fixture.Model->RefreshPendingItemCommandCount();
	TestFalse(TEXT("The last empty slot is reserved, not available to unequip"),
		Fixture.Model->RequestUnequipToFirstFreeSlot(Helm.GetItemId(), RequestId));
	const FText ReservedFeedback = Fixture.Model->LocalActionFeedback;
	TestFalse(TEXT("Reservations are explained separately from an actually full bag"),
		ReservedFeedback.IsEmpty() || ReservedFeedback.EqualTo(FullFeedback));
	TestEqual(TEXT("Local refusal does not release another request"), Fixture.Model->PendingItemCommandCount, 1);

	Fixture.Model->bHasInventoryRecovery = true;
	TestFalse(TEXT("Recovery feedback takes precedence over local refusal"), Fixture.Model->GetCommandFeedbackText().EqualTo(ReservedFeedback));
	Fixture.Model->bInventoryRecoveryStorageBlocked = true;
	TestFalse(TEXT("Recovery storage failure takes precedence too"), Fixture.Model->GetCommandFeedbackText().EqualTo(ReservedFeedback));
	Fixture.Model->bInventoryAuthorityRevoked = true;
	TestTrue(TEXT("Terminal authority never displays earlier local feedback"), Fixture.Model->GetCommandFeedbackText().IsEmpty());
	Fixture.Model->bInventoryAuthorityRevoked = false;
	Fixture.Model->bInventoryRecoveryStorageBlocked = false;
	Fixture.Model->bHasInventoryRecovery = false;
	FRPGItemCommandClientResult Refused;
	Refused.RequestId = PendingId; Refused.Operation = TEXT("Item.Move");
	Refused.Result = ERPGItemCommandResultCode::Conflict;
	Fixture.Model->HandleCommandCompleted(Refused); // Native ACK fixture, not an HTTP or UI-input test.
	TestTrue(TEXT("The next server result clears earlier local feedback"), Fixture.Model->LocalActionFeedback.IsEmpty());
	TestEqual(TEXT("Only the server callback replaces the server result"), Fixture.Model->LastCommandResult.RequestId, PendingId);
	TestEqual(TEXT("Definitive refusal retires the original reservation"), Fixture.Model->PendingItemCommandCount, 0);

	FRPGItemActionPolicy NoAction;
	NoAction.DefinitionId = Fixture.ConsumableId; NoAction.DefinitionVersion = 1;
	Fixture.Policies.RegisterPolicy(NoAction);
	TestFalse(TEXT("A non-usable item produces fresh local feedback without a command"),
		Fixture.Model->RequestDefaultActionUsingPolicies(Records[1].GetItemId(), Fixture.Policies, RequestId));
	TestFalse(TEXT("Local feedback exists before teardown"), Fixture.Model->LocalActionFeedback.IsEmpty());
	Fixture.Model->InitializeFromPlayerController(nullptr);
	TestTrue(TEXT("Owner teardown clears local feedback"), Fixture.Model->LocalActionFeedback.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemUnequipInteractionTest,
	"ProjectRPG.Item.Interaction.EquipmentCellUsesFreeInventorySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemUnequipInteractionTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("ViewModel fixture"), Fixture.Model)) return false;
	const auto Inventory = ERPGItemContainerType::Inventory;
	const auto Equipment = ERPGItemContainerType::Equipment;
	const auto Helm = Fixture.MakeRecord(Fixture.EquipmentId, Equipment,
		static_cast<int32>(EEquipmentSlotType::Head));
	const auto Occupant = Fixture.MakeRecord(Fixture.ConsumableId, Inventory, 0, 3);
	if (!TestTrue(TEXT("Load equipment and occupied inventory slot"),
		Fixture.Load({ Helm, Occupant }))) return false;
	FGuid RequestId;
	TestFalse(TEXT("Occupied inventory slot is rejected"),
		Fixture.Model->RequestUnequip(Helm.GetItemId(), 0, RequestId));
	TestFalse(TEXT("Out-of-range slot is rejected"), Fixture.Model->RequestUnequip(
		Helm.GetItemId(), RPGItemInventoryPolicy::SlotCapacity, RequestId));
	TestFalse(TEXT("Inventory item cannot be unequipped"),
		Fixture.Model->RequestUnequipToFirstFreeSlot(Occupant.GetItemId(), RequestId));
	URPGInventoryProjectionItemSlotWidget* Cell =
		NewObject<URPGInventoryProjectionItemSlotWidget>(Fixture.Controller);
	Cell->SetItem(Helm.GetItemId(), FText::FromString(TEXT("Helm")), Fixture.Model, true);
	TestTrue(TEXT("Equipment cell submits unequip via the ViewModel"), Cell->RequestDefaultAction());
	TestEqual(TEXT("Facade receives unequip"), Fixture.Model->LastCommandResult.Operation,
		FName(TEXT("Item.Unequip")));
	FRPGInventoryProjectionEntry Entry;
	Fixture.Projection->FindProjectedItem(Helm.GetItemId(), Entry);
	TestEqual(TEXT("UI does not unequip before backend confirmation"), Entry.GetContainerType(), Equipment);
	TArray<FRPGItemRecord> FullBag{ Helm };
	for (int32 Slot = 0; Slot < RPGItemInventoryPolicy::SlotCapacity; ++Slot)
	{
		FullBag.Add(Fixture.MakeRecord(Fixture.ConsumableId, Inventory, Slot));
	}
	if (!TestTrue(TEXT("Load a full inventory"), Fixture.Load(FullBag))) return false;
	TestFalse(TEXT("Full inventory blocks equipment cell action"), Cell->RequestDefaultAction());
	TestFalse(TEXT("Full inventory blocks auto-unequip"),
		Fixture.Model->RequestUnequipToFirstFreeSlot(Helm.GetItemId(), RequestId));
	TestFalse(TEXT("No command is minted for a full bag"), RequestId.IsValid());
	Cell->SetItem(Helm.GetItemId(), FText::GetEmpty(), nullptr, true);
	TestFalse(TEXT("Unbound cell cannot submit a command"), Cell->RequestDefaultAction());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemEmptyCellPresentationTest,
	"ProjectRPG.Item.Presentation.EmptyEquipmentKeepsSlotLabel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemEmptyCellPresentationTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("ViewModel fixture"), Fixture.Model) || !Fixture.Load({})) return false;
	// NativeOnInitialized requires a valid player context to build the native cell tree.
	auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Fixture.Controller->Player = LocalPlayer;
	LocalPlayer->PlayerController = Fixture.Controller;
	Fixture.Controller->SetAsLocalPlayerController();
	Fixture.World->AddController(Fixture.Controller);
	auto* Cell = CreateWidget<URPGInventoryProjectionItemSlotWidget>(Fixture.Controller);
	if (!TestNotNull(TEXT("Native item cell"), Cell)) return false;
	Cell->SetItem(FGuid(), FText::FromString(TEXT("HEAD")), Fixture.Model, true,
		static_cast<int32>(EEquipmentSlotType::Head));
	const TSharedRef<SWidget> SlateCell = Cell->TakeWidget();
	TestTrue(TEXT("Native cell has a visible Slate view"), SlateCell->GetVisibility().IsVisible());
	auto* Label = Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ItemFallbackName")));
	if (!TestNotNull(TEXT("Fallback label layer"), Label)) return false;
	TestEqual(TEXT("Empty equipment keeps its supplied slot name"), Label->GetText().ToString(), FString(TEXT("HEAD")));
	TestEqual(TEXT("Empty equipment label is visible without blocking input"),
		Label->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("An empty equipment slot cannot submit an item action"), Cell->RequestDefaultAction());

	Cell->SetItem(FGuid(), FText::FromString(TEXT("CHEST")), Fixture.Model, true,
		static_cast<int32>(EEquipmentSlotType::Chest));
	Cell->RefreshInteractionState();
	TestEqual(TEXT("Reusing an empty equipment cell updates its slot name"),
		Label->GetText().ToString(), FString(TEXT("CHEST")));
	Cell->SetItem(FGuid(), FText::FromString(TEXT("Stale equipment label")), Fixture.Model, false, 0);
	TestTrue(TEXT("An empty bag cell never inherits an equipment label"), Label->GetText().IsEmpty());
	TestEqual(TEXT("An empty bag label is hidden"), Label->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("An empty bag cell clears the tooltip"), Cell->GetToolTipText().IsEmpty());
	TestFalse(TEXT("Presentation does not submit a command"), Fixture.Model->bHasLastCommandResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemDefinitionPresentationTest,
	"ProjectRPG.Item.Presentation.UsesMatchingNativeAndLegacyDefinitionText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemDefinitionPresentationTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("ViewModel fixture"), Fixture.Model)) return false;
	const FPrimaryAssetId IronId{TEXT("RPGItemDefinition"), TEXT("GameItem.Equipment.Helm.Iron")};
	const FPrimaryAssetId LeatherId{TEXT("RPGItemDefinition"), TEXT("GameItem.Equipment.Helm.Leather")};
	const auto Iron = Fixture.MakeRecord(IronId, ERPGItemContainerType::Inventory, 2);
	const auto Leather = Fixture.MakeRecord(LeatherId, ERPGItemContainerType::Equipment, 0);
	const auto Mail = Fixture.MakeRecord(IronId, ERPGItemContainerType::Mail, 0);
	if (!TestTrue(TEXT("Owned inventory, equipment and mail projected"), Fixture.Load({Iron, Leather, Mail}))) return false;
	FRPGItemDefinitionRegistry Catalog;
	FRPGItemDefinitionView View;
	View.Snapshot = {IronId, 1, 1};
	View.ItemTag = RPGGameplayTags::GameItem_Equipment_Helm_Default;
	View.ItemCategory = EItemCategory::Equip;
	View.DisplayName = FText::FromString(TEXT("Iron Helm"));
	View.Description = FText::FromString(TEXT("An iron helmet."));
	if (!TestTrue(TEXT("Native definition registered"), Catalog.RegisterDefinitionView(View))) return false;
	View.Snapshot.DefinitionId = LeatherId;
	View.DisplayName = FText::FromString(TEXT("Leather Cap"));
	View.Description = FText::FromString(TEXT("A light leather cap."));
	View.bLegacySource = true;
	if (!TestTrue(TEXT("Existing legacy adapter view registered"), Catalog.RegisterDefinitionView(View))) return false;
	const auto IronText = Fixture.Model->GetItemPresentationUsingCatalog(Iron.GetItemId(), &Catalog);
	const auto LeatherText = Fixture.Model->GetItemPresentationUsingCatalog(Leather.GetItemId(), &Catalog);
	const auto MailText = Fixture.Model->GetItemPresentationUsingCatalog(Mail.GetItemId(), &Catalog);
	TestEqual(TEXT("Inventory uses authored name, not the common ID prefix"), IronText.DisplayName.ToString(), FString(TEXT("Iron Helm")));
	TestEqual(TEXT("Native description is available"), IronText.Description.ToString(), FString(TEXT("An iron helmet.")));
	TestEqual(TEXT("Equipped legacy item uses the same presentation boundary"), LeatherText.DisplayName.ToString(), FString(TEXT("Leather Cap")));
	TestEqual(TEXT("Legacy view description is preserved"), LeatherText.Description.ToString(), FString(TEXT("A light leather cap.")));
	TestEqual(TEXT("Mail uses the same matching definition name"), MailText.DisplayName.ToString(), FString(TEXT("Iron Helm")));
	TestEqual(TEXT("Mail description is available before claiming"), MailText.Description.ToString(), FString(TEXT("An iron helmet.")));
	TestEqual(TEXT("Mail quantity remains owner-projected"), MailText.Quantity, Mail.GetQuantity());
	TestFalse(TEXT("Presentation never submits an item command"), Fixture.Model->bHasLastCommandResult);
	FRPGInventoryProjectionEntry Unchanged;
	TestTrue(TEXT("Reading mail details never claims the attachment"), Fixture.Projection->FindMailItem(Mail.GetItemId(), Unchanged));
	TestTrue(TEXT("Read-only presentation preserves the projected record"), Fixture.Projection->FindProjectedItem(Iron.GetItemId(), Unchanged));
	TestEqual(TEXT("Full revision is unchanged"), Unchanged.GetRevision(), Iron.GetRevision());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemPresentationFallbackTest,
	"ProjectRPG.Item.Presentation.PreservesIdentityWhenDefinitionTextIsUnavailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemPresentationFallbackTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("ViewModel fixture"), Fixture.Model)) return false;
	const auto Item = Fixture.MakeRecord(Fixture.EquipmentId, ERPGItemContainerType::Inventory, 0);
	if (!TestTrue(TEXT("Owned item projected"), Fixture.Load({Item}))) return false;
	const FString Identity = Fixture.EquipmentId.PrimaryAssetName.ToString();
	const auto Missing = Fixture.Model->GetItemPresentationUsingCatalog(Item.GetItemId(), nullptr);
	TestEqual(TEXT("Missing catalog keeps the complete definition identity"), Missing.DisplayName.ToString(), Identity);
	TestTrue(TEXT("Missing metadata does not invent a description"), Missing.Description.IsEmpty());
	struct FFaultyCatalog final : IRPGItemDefinitionViewCatalog
	{
		FRPGItemDefinitionView View;
		virtual bool TryFindDefinitionView(const FPrimaryAssetId&, FRPGItemDefinitionView& Out) const override
		{
			Out = View;
			return true;
		}
	} Catalog;
	Catalog.View.Snapshot = {Fixture.EquipmentId, 2, 1};
	Catalog.View.ItemTag = RPGGameplayTags::GameItem_Equipment_Helm_Default;
	Catalog.View.ItemCategory = EItemCategory::Equip;
	Catalog.View.DisplayName = FText::FromString(TEXT("Wrong metadata"));
	Catalog.View.Description = FText::FromString(TEXT("Do not display this description."));
	const auto WrongVersion = Fixture.Model->GetItemPresentationUsingCatalog(Item.GetItemId(), &Catalog);
	TestEqual(TEXT("A newer definition version cannot relabel an older record"), WrongVersion.DisplayName.ToString(), Identity);
	TestTrue(TEXT("Wrong-version description is hidden"), WrongVersion.Description.IsEmpty());
	Catalog.View.Snapshot = {Fixture.ConsumableId, 1, 1};
	const auto WrongId = Fixture.Model->GetItemPresentationUsingCatalog(Item.GetItemId(), &Catalog);
	TestEqual(TEXT("A provider cannot substitute another definition"), WrongId.DisplayName.ToString(), Identity);
	TestTrue(TEXT("Wrong-identity description is hidden"), WrongId.Description.IsEmpty());
	Catalog.View.Snapshot = {Fixture.EquipmentId, 1, 1};
	Catalog.View.DisplayName = FText::GetEmpty();
	TestEqual(TEXT("Invalid view also preserves the fallback"), Fixture.Model->GetItemPresentationUsingCatalog(Item.GetItemId(), &Catalog).DisplayName.ToString(), Identity);
	TestTrue(TEXT("Unknown item has no stale text"), Fixture.Model->GetItemPresentationUsingCatalog(FGuid::NewGuid(), &Catalog).DisplayName.IsEmpty());
	TestTrue(TEXT("Invalid item has no stale text"), Fixture.Model->GetItemPresentationUsingCatalog(FGuid(), &Catalog).DisplayName.IsEmpty());
	URPGInventoryProjectionItemSlotWidget* Cell = NewObject<URPGInventoryProjectionItemSlotWidget>(Fixture.Controller);
	Cell->SetItem(Item.GetItemId(), FText::GetEmpty(), Fixture.Model);
	TestTrue(TEXT("The actual cell tooltip includes the complete identity fallback"), Cell->GetToolTipText().ToString().Contains(Identity));
	TestTrue(TEXT("The cell keeps the existing input hint"), Cell->GetToolTipText().ToString().Contains(TEXT("Right-click")));
	Cell->SetItem(FGuid(), FText::GetEmpty(), nullptr);
	TestFalse(TEXT("Reusing an empty cell clears the previous item details"), Cell->GetToolTipText().ToString().Contains(Identity));
	TestFalse(TEXT("Read-only text never submits an item command"), Fixture.Model->bHasLastCommandResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentProgressionDetailsTest,
	"ProjectRPG.Item.Presentation.EquipmentProgressionDetails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentProgressionDetailsTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Existing owner ViewModel"), Fixture.Model)) return false;
	for (int32 Level = 0; Level <= 3; ++Level)
	{
		FRPGEquipmentProgressionState Growth;
		Growth.GrowthBandId = TEXT("FirstSlice"); Growth.RuleVersion = 1; Growth.Level = Level;
		const auto Item = Fixture.MakeRecord(Fixture.EquipmentId, ERPGItemContainerType::Inventory, 0, 1, Growth);
		if (!TestTrue(TEXT("Persisted optional state enters the owner projection"), Fixture.Load({Item}))) return false;
		const auto Presentation = Fixture.Model->GetItemPresentation(Item.GetItemId());
		TestTrue(TEXT("Every explicit level including +0 reaches existing instance details"),
			Presentation.InstanceDetails.ToString().Contains(FString::Printf(TEXT("+%d"), Level)));
		TestFalse(TEXT("No attack contribution or GAS effect is invented for display"),
			Presentation.InstanceDetails.ToString().Contains(TEXT("Attack")));
		FRPGInventoryProjectionEntry Unchanged;
		if (!TestTrue(TEXT("Reading details preserves the owner entry"), Fixture.Projection->FindProjectedItem(Item.GetItemId(), Unchanged))) return false;
		TestTrue(TEXT("Reading does not initialize/change the persisted stage"), Unchanged.GetEquipmentProgression() == Growth);
		TestEqual(TEXT("Full Int64 revision is untouched"), Unchanged.GetRevision(), Item.GetRevision());
	}
	const auto Legacy = Fixture.MakeRecord(Fixture.EquipmentId, ERPGItemContainerType::Inventory, 0);
	if (!TestTrue(TEXT("Legacy/unset item remains readable"), Fixture.Load({Legacy}))) return false;
	TestFalse(TEXT("No automatic +0 is shown for a legacy item"),
		Fixture.Model->GetItemPresentation(Legacy.GetItemId()).InstanceDetails.ToString().Contains(TEXT("+0")));
	TestTrue(TEXT("An invalid item has no stale growth label"), Fixture.Model->GetItemPresentation(FGuid()).InstanceDetails.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentProgressionCellTest,
	"ProjectRPG.Item.Presentation.EquipmentProgressionCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentProgressionCellTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Existing owner ViewModel"), Fixture.Model)) return false;
	for (const auto Container : { ERPGItemContainerType::Inventory, ERPGItemContainerType::Equipment, ERPGItemContainerType::Mail })
	{
		FRPGEquipmentProgressionState Growth;
		Growth.GrowthBandId = TEXT("FirstSlice"); Growth.RuleVersion = 1; Growth.Level = 0;
		const int32 Slot = Container == ERPGItemContainerType::Equipment ? static_cast<int32>(EEquipmentSlotType::Head) : 0;
		const auto Item = Fixture.MakeRecord(Fixture.EquipmentId, Container, Slot, 1, Growth);
		if (!TestTrue(TEXT("Inventory/equipped/mail state loads through the production mapper"), Fixture.Load({Item}))) return false;
		auto* Cell = NewObject<URPGInventoryProjectionItemSlotWidget>(Fixture.Controller);
		Cell->SetItem(Item.GetItemId(), FText::GetEmpty(), Fixture.Model, Container == ERPGItemContainerType::Equipment, Slot);
		TestTrue(TEXT("The actual native cell used by WBP includes explicit +0"), Cell->GetToolTipText().ToString().Contains(TEXT("+0")));

		Growth.Level = 3;
		FRPGItemInstanceState NextState;
		if (!TestTrue(TEXT("Updated persisted instance state restores without a UI mutation"),
			FRPGItemInstanceState::TryRestore(Item.GetItemId(), 31, 1, {}, {}, NextState, Growth))) return false;
		FRPGItemRecord Next;
		if (!TestTrue(TEXT("A new authoritative revision retains the same instance ID"),
			FRPGItemRecord::TryRestore(Item.GetDefinitionId(), 1, Fixture.Owner, Item.GetLocation(), NextState,
				Item.GetRevision() + 1, ERPGItemLifecycleState::Active, Item.GetMetadata(), Next))) return false;
		if (!TestTrue(TEXT("A refreshed owner snapshot applies"), Fixture.Load({Next}))) return false;
		// The existing grid refresh calls SetItem; no new binding, command or UI screen.
		Cell->SetItem(Next.GetItemId(), FText::GetEmpty(), Fixture.Model, Container == ERPGItemContainerType::Equipment, Slot);
		TestTrue(TEXT("The same cell shows the new persisted +3"), Cell->GetToolTipText().ToString().Contains(TEXT("+3")));
		TestFalse(TEXT("The earlier +0 is not retained"), Cell->GetToolTipText().ToString().Contains(TEXT("+0")));
		Cell->SetItem(FGuid(), FText::GetEmpty(), nullptr);
		TestTrue(TEXT("Clearing/reusing the cell clears growth details"), Cell->GetToolTipText().IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMailClaimPresentationTest,
	"ProjectRPG.Item.Mail.ClaimPresentationUsesOwnerStateWithoutSubmitting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMailClaimPresentationTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	using EState = ERPGMailClaimState;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Mail presentation fixture"), Fixture.Model)) return false;
	const auto Mail = Fixture.MakeRecord(Fixture.ConsumableId, ERPGItemContainerType::Mail, 0);
	if (!TestTrue(TEXT("Owner mail loaded"), Fixture.Load({Mail}))) return false;
	const auto State = [&Fixture, &Mail]() { return Fixture.Model->GetMailClaimPresentation(Mail.GetItemId()).State; };
	TestEqual(TEXT("Free bag permits a local claim attempt"), State(), EState::Available);
	TestTrue(TEXT("Available claim has no blocking reason"), Fixture.Model->GetMailClaimPresentation(Mail.GetItemId()).Reason.IsEmpty());
	TestEqual(TEXT("Invalid identity is unavailable"), Fixture.Model->GetMailClaimPresentation(FGuid()).State, EState::Unavailable);
	TestEqual(TEXT("Unknown identity is unavailable"), Fixture.Model->GetMailClaimPresentation(FGuid::NewGuid()).State, EState::Unavailable);

	Fixture.Model->PendingMailItemIds.Add(Mail.GetItemId());
	TestEqual(TEXT("Original claim remains pending"), State(), EState::Pending);
	Fixture.Model->PendingMailItemIds.Reset();
	for (int32 SlotIndex = 0; SlotIndex < RPGItemInventoryPolicy::SlotCapacity; ++SlotIndex)
		Fixture.Model->PendingMailSlotsByItem.Add(FGuid::NewGuid(), SlotIndex);
	TestEqual(TEXT("Reserved empty slots are not a physically full bag"), State(), EState::InventoryReserved);
	Fixture.Model->PendingMailSlotsByItem.Reset();
	TestEqual(TEXT("Released reservations make room available again"), State(), EState::Available);

	TArray<FRPGItemRecord> FullBag{Mail};
	for (int32 SlotIndex = 0; SlotIndex < RPGItemInventoryPolicy::SlotCapacity; ++SlotIndex)
		FullBag.Add(Fixture.MakeRecord(Fixture.ConsumableId, ERPGItemContainerType::Inventory, SlotIndex));
	if (!TestTrue(TEXT("Full bag and untouched mail loaded"), Fixture.Load(FullBag))) return false;
	TestEqual(TEXT("Occupied capacity is full"), State(), EState::InventoryFull);
	TestFalse(TEXT("Full state exposes a reason without requiring a click"), Fixture.Model->GetMailClaimPresentation(Mail.GetItemId()).Reason.IsEmpty());
	Fixture.Model->bHasInventoryRecovery = true;
	TestEqual(TEXT("Recovery takes priority over full bag"), State(), EState::RecoveryRequired);
	Fixture.Model->bInventoryRecoveryStorageBlocked = true;
	TestEqual(TEXT("Unavailable recovery storage has a separate reason"), State(), EState::RecoveryStorageBlocked);
	Fixture.Model->bHasInventoryRecovery = false;
	Fixture.Model->bInventoryRecoveryStorageBlocked = false;
	Fixture.Model->LoadState = ERPGInventoryProjectionLoadState::Uninitialized;
	TestEqual(TEXT("Unloaded state is not misreported as full"), State(), EState::Unavailable);

	FRPGItemRecordMetadata Metadata = Mail.GetMetadata();
	Metadata.bLocked = true;
	FRPGItemRecord Locked;
	if (!TestTrue(TEXT("Locked owner record restored"), FRPGItemRecord::TryRestore(Mail.GetDefinitionId(),
		Mail.GetDefinitionVersion(), Mail.GetOwner(), Mail.GetLocation(), Mail.GetState(), Mail.GetRevision() + 1,
		Mail.GetLifecycleState(), Metadata, Locked)) || !Fixture.Load({Locked})) return false;
	TestEqual(TEXT("Projected lock prevents the local claim attempt"), State(), EState::Locked);
	Fixture.Model->bInventoryAuthorityRevoked = true;
	TestEqual(TEXT("Revocation overrides prior item availability"), State(), EState::Unavailable);
	TestFalse(TEXT("Presentation reads never submit a claim"), Fixture.Model->bHasLastCommandResult);
	FRPGInventoryProjectionEntry Unchanged;
	TestTrue(TEXT("Reward remains in the mail projection"), Fixture.Projection->FindMailItem(Mail.GetItemId(), Unchanged));
	TestEqual(TEXT("Presentation did not advance the item revision"), Unchanged.GetRevision(), Locked.GetRevision());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemPendingActionPresentationTest,
	"ProjectRPG.Item.Presentation.SharedOwnerAndPendingActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemPendingActionPresentationTest::RunTest(const FString&)
{
	using namespace RPGItemProjectionInteractionTests;
	using EContainer = ERPGItemContainerType;
	using EState = ERPGItemSlotPresentationState;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Pending action fixture"), Fixture.Model)) return false;
	const auto Helm = Fixture.MakeRecord(Fixture.EquipmentId, EContainer::Inventory, 0);
	const auto Other = Fixture.MakeRecord(Fixture.EquipmentId, EContainer::Inventory, 1);
	const auto Potion = Fixture.MakeRecord(Fixture.ConsumableId, EContainer::Inventory, 2, 3);
	if (!TestTrue(TEXT("Owned items loaded"), Fixture.Load({Helm, Other, Potion}))) return false;
	const FGuid EquipRequest = FGuid::NewGuid();
	URPGInventoryProjectionViewModel::FPendingItemAction Equip;
	Equip.ItemId = Helm.GetItemId(); Equip.ExpectedRevision = Helm.GetRevision();
	Equip.SourceContainer = EContainer::Inventory; Equip.SourceSlot = 0;
	Equip.DestinationContainer = EContainer::Equipment; Equip.DestinationSlot = static_cast<int32>(EEquipmentSlotType::Head);
	Fixture.Model->PendingItemActionsByRequest.Add(EquipRequest, Equip);
	Fixture.Model->RefreshPendingItemCommandCount();
	TestEqual(TEXT("Action participates in shared pending count"), Fixture.Model->PendingItemCommandCount, 1);
	TestEqual(TEXT("Original item waits"), Fixture.Model->GetItemSlotPresentationState(Helm.GetItemId(), EContainer::Inventory), EState::Pending);
	TestEqual(TEXT("Empty equipment destination waits"), Fixture.Model->GetItemSlotPresentationState(FGuid(), EContainer::Equipment, Equip.DestinationSlot), EState::Pending);
	FGuid Rejected;
	TestFalse(TEXT("Pending item cannot also be consumed"), Fixture.Model->RequestConsume(Helm.GetItemId(), Rejected));
	TestFalse(TEXT("Another item cannot claim reserved equipment"), Fixture.Model->RequestEquip(Other.GetItemId(), EEquipmentSlotType::Head, Rejected));
	TestFalse(TEXT("Pending item cannot also be moved"), Fixture.Model->RequestMoveItem(Helm.GetItemId(), Helm.GetRevision(), 3, Rejected));
	TestFalse(TEXT("Local refusal mints no request"), Rejected.IsValid());
	FRPGItemCommandClientResult Ack;
	Ack.RequestId = EquipRequest; Ack.Operation = TEXT("Item.Equip"); Ack.Result = ERPGItemCommandResultCode::Succeeded;
	Fixture.Model->HandleCommandCompleted(Ack);
	TestTrue(TEXT("Success ACK alone holds destination"), Fixture.Model->IsEquipmentSlotReserved(Equip.DestinationSlot));
	FRPGItemLocation EquipmentLocation = Helm.GetLocation();
	EquipmentLocation.ContainerType = EContainer::Equipment; EquipmentLocation.SlotIndex = Equip.DestinationSlot;
	FRPGItemRecord Equipped;
	if (!TestTrue(TEXT("Authoritative equipment state restored"), Helm.TryEquipTo(EquipmentLocation, Equipped))) return false;
	Equipped = Equipped.CopyWithRevision(Helm.GetRevision() + 1);
	if (!TestTrue(TEXT("New equipment revision applied"), Fixture.Load({Equipped, Other, Potion}))) return false;
	TestEqual(TEXT("New revision retires action"), Fixture.Model->PendingItemCommandCount, 0);
	TestFalse(TEXT("Retired action releases equipment reservation"), Fixture.Model->IsEquipmentSlotReserved(Equip.DestinationSlot));

	const FGuid UnequipRequest = FGuid::NewGuid();
	URPGInventoryProjectionViewModel::FPendingItemAction Unequip;
	Unequip.ItemId = Equipped.GetItemId(); Unequip.ExpectedRevision = Equipped.GetRevision();
	Unequip.SourceContainer = EContainer::Equipment; Unequip.SourceSlot = EquipmentLocation.SlotIndex;
	Unequip.DestinationContainer = EContainer::Inventory; Unequip.DestinationSlot = 3;
	Fixture.Model->PendingItemActionsByRequest.Add(UnequipRequest, Unequip);
	Fixture.Model->RefreshPendingItemCommandCount();
	TestEqual(TEXT("Unequip reserves one empty bag slot"), Fixture.Model->GetInventoryCapacity().ReservedEmptySlots, 1);
	TestFalse(TEXT("Pending equipment cannot be unequipped twice"), Fixture.Model->RequestUnequip(Equipped.GetItemId(), 4, Rejected));
	TestFalse(TEXT("A bag move cannot occupy pending unequip destination"), Fixture.Model->RequestMoveItem(Other.GetItemId(), Other.GetRevision(), 3, Rejected));
	Ack.RequestId = UnequipRequest; Ack.Operation = TEXT("Item.Unequip"); Ack.Result = ERPGItemCommandResultCode::Conflict;
	Fixture.Model->HandleCommandCompleted(Ack);
	TestFalse(TEXT("Definitive refusal releases unequip reservation"), Fixture.Model->IsInventorySlotReserved(3));

	const FGuid ConsumeRequest = FGuid::NewGuid();
	URPGInventoryProjectionViewModel::FPendingItemAction Consume;
	Consume.ItemId = Potion.GetItemId(); Consume.ExpectedRevision = Potion.GetRevision();
	Consume.SourceContainer = EContainer::Inventory; Consume.SourceSlot = 2;
	Fixture.Model->PendingItemActionsByRequest.Add(ConsumeRequest, Consume);
	Ack.RequestId = ConsumeRequest; Ack.Operation = TEXT("Item.Consume"); Ack.Result = ERPGItemCommandResultCode::Uncertain;
	Fixture.Model->HandleCommandCompleted(Ack);
	TestTrue(TEXT("Unknown result is not a definitive refusal"), Fixture.Model->IsItemActionPending(Potion.GetItemId()));
	FRPGItemRecord Consumed;
	if (!TestTrue(TEXT("Updated stack created"), Potion.TrySetQuantity(2, Consumed))) return false;
	Consumed = Consumed.CopyWithRevision(Potion.GetRevision() + 1);
	if (!TestTrue(TEXT("New stack revision applied"), Fixture.Load({Equipped, Other, Consumed}))) return false;
	TestFalse(TEXT("New stack revision retires consume reservation"), Fixture.Model->IsItemActionPending(Potion.GetItemId()));

	auto* Controller = Fixture.World->SpawnActor<ARPGPlayerController>();
	if (!TestNotNull(TEXT("Local RPG controller"), Controller)) return false;
	Controller->SetAsLocalPlayerController(); // Isolated worlds have no player admission to establish this role.
	auto* Shared = Controller->GetInventoryProjectionViewModel();
	if (!TestNotNull(TEXT("Controller-scoped shared presentation"), Shared)) return false;
	auto* SharedProjection = Controller->FindComponentByClass<URPGInventoryProjectionComponent>();
	if (!TestNotNull(TEXT("Shared controller projection"), SharedProjection) ||
		!TestTrue(TEXT("Shared owned stack loaded"), SharedProjection->ApplyAuthoritativeRecords(Fixture.Owner, {Potion})) ||
		!TestTrue(TEXT("Shared snapshot ready"), SharedProjection->ApplyCompletedRewardSnapshot(
			Fixture.Owner, {Potion}, SharedProjection->GetAuthoritativeSnapshotGeneration()))) return false;
	auto* Pawn = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("Quickslot owner pawn"), Pawn)) return false;
	Controller->Possess(Pawn);
	auto* QuickSlots = NewObject<UQuickSlotComponent>(Pawn);
	Pawn->AddInstanceComponent(QuickSlots); QuickSlots->RegisterComponent();
	QuickSlots->BindInventoryProjection(SharedProjection, &Fixture.Policies);
	QuickSlots->SetItemSlotById(0, Potion.GetItemId());
	QuickSlots->SetItemSlotById(1, Potion.GetItemId());
	if (!TestEqual(TEXT("First shortcut assigned"), QuickSlots->GetItemSlotContent(0)->ItemId, Potion.GetItemId()) ||
		!TestEqual(TEXT("Alias shortcut assigned"), QuickSlots->GetItemSlotContent(1)->ItemId, Potion.GetItemId())) return false;
	Shared->PendingItemActionsByRequest.Add(ConsumeRequest, Consume);
	Shared->RefreshPendingItemCommandCount();
	QuickSlots->UseItemSlot(0, Controller);
	QuickSlots->UseItemSlot(1, Controller);
	TestFalse(TEXT("Both shortcuts respect an existing bag reservation"), Shared->bHasLastCommandResult);
	TestEqual(TEXT("Aliases do not add pending requests"), Shared->PendingItemCommandCount, 1);
	FGuid Denied = FGuid::NewGuid();
	TestFalse(TEXT("Controller consumption shares the same reservation"), Controller->TryConsumeInventoryItem(Potion.GetItemId(), Denied));
	TestFalse(TEXT("Blocked controller input returns no new request"), Denied.IsValid());
	TestTrue(TEXT("Repeated window access shares the same model"), Shared == Controller->GetInventoryProjectionViewModel());
	TestTrue(TEXT("Repeated window access preserves reservations"), Shared->PendingItemActionsByRequest.Contains(ConsumeRequest));
	TestTrue(TEXT("Binding belongs to the exact controller"), Shared->IsBoundToPlayerController(Controller));
	Shared->PendingItemActionsByRequest.Reset();
	Shared->RefreshPendingItemCommandCount();
	QuickSlots->UseItemSlot(0, Fixture.Controller);
	TestFalse(TEXT("Foreign controller cannot submit a quickslot command"), Shared->bHasLastCommandResult);
	QuickSlots->UseItemSlot(1, Controller);
	TestTrue(TEXT("Released reservation allows the owning shortcut through the shared model"), Shared->bHasLastCommandResult);
	TestTrue(TEXT("Shared model observes the actual consume request"), Shared->LastCommandResult.RequestId.IsValid());
	TestEqual(TEXT("Quickslot still requests only consume"), Shared->LastCommandResult.Operation, FName(TEXT("Item.Consume")));
	QuickSlots->BindInventoryProjection(nullptr);
	Shared->InitializeFromPlayerController(nullptr);
	TestEqual(TEXT("Explicit owner teardown clears local handles"), Shared->PendingItemCommandCount, 0);
	TestFalse(TEXT("Detached model is no longer bound"), Shared->IsBoundToPlayerController(Controller));
	return true;
}

#endif
