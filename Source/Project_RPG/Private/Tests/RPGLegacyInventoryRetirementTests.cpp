#if WITH_DEV_AUTOMATION_TESTS

#include "Component/RPGInventoryComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Item/RPGItemBase.h"
#include "Misc/AutomationTest.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UObject/UnrealType.h"

namespace RPGLegacyInventoryRetirementTests
{
struct FFixture
{
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	URPGInventoryComponent* Legacy = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	FRPGItemOwnerRef Owner;
	FRPGItemRecord Record;

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
		Legacy = NewObject<URPGInventoryComponent>(Controller);
		Controller->AddInstanceComponent(Legacy);
		Legacy->RegisterComponent();
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection);
		Projection->RegisterComponent();
		URPGItemCommandComponent* Commands = NewObject<URPGItemCommandComponent>(Controller);
		Controller->AddInstanceComponent(Commands);
		Commands->RegisterComponent();
		Model = NewObject<URPGInventoryProjectionViewModel>(Controller);
		Model->InitializeFromPlayerController(Controller);
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 19, 3, {}, {}, State);
		FRPGItemLocation Location;
		Location.ContainerType = ERPGItemContainerType::Inventory;
		Location.ContainerId = Owner.OwnerId;
		Location.SlotIndex = 7;
		FRPGItemRecord::TryRestore(
			FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("LegacyRetirementPotion")),
			1, Owner, Location, State, 9007199254740993LL,
			ERPGItemLifecycleState::Active, {}, Record);
		Projection->ApplyAuthoritativeRecords(Owner, { Record });
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

	URPGItemBase* MakeAlias(const FGuid& ItemId) const
	{
		URPGItemBase* Item = NewObject<URPGItemBase>(Controller);
		Item->SetInstanceId(ItemId);
		Item->SetTotalQuantity(999); // Deliberately not the authoritative quantity.
		Item->SetSlotIndex(2);
		return Item;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLegacyInventoryWriteRetirementTest,
	"ProjectRPG.Item.LegacyRetirement.NoSecondaryStateOrUnsafeWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGLegacyInventoryWriteRetirementTest::RunTest(const FString&)
{
	using namespace RPGLegacyInventoryRetirementTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Legacy compatibility facade"), Fixture.Legacy)) return false;
	TestNull(TEXT("No legacy replicated inventory property"),
		FindFProperty<FProperty>(URPGInventoryComponent::StaticClass(), TEXT("InventoryList")));
	TestNull(TEXT("No legacy persistence callback"),
		URPGInventoryComponent::StaticClass()->FindFunctionByName(TEXT("OnWebInventoryLoaded")));
	TestNull(TEXT("No legacy widget is created"), Fixture.Legacy->GetInventoryMenu());
	TestTrue(TEXT("No second inventory list"), Fixture.Legacy->GetAllItems().IsEmpty());

	URPGItemBase* Alias = Fixture.MakeAlias(Fixture.Record.GetItemId());
	URPGItemBase* Other = Fixture.MakeAlias(FGuid::NewGuid());
	Fixture.Legacy->Server_DropItem(Alias, 1);
	Fixture.Legacy->Server_MoveItem(Alias, 6);
	Fixture.Legacy->Server_SplitItem(Alias, 1, 6);
	Fixture.Legacy->Server_TransferItemQuantity(Alias, Other, 1);
	Fixture.Legacy->OrganizeInventory();
	Fixture.Legacy->Server_OrganizeInventory(EItemCategory::Consume);
	TestFalse(TEXT("Manifest drop cannot bypass the native debit"),
		Fixture.Legacy->SpawnDroppedItem(Alias, 1));
	Fixture.Legacy->AddRepSubObj(Alias);
	Fixture.Legacy->NotifyItemUpdated(Alias);
	Fixture.Legacy->RemoveRepSubObj(Alias);
	TestEqual(TEXT("Legacy object quantity is not mutated"), Alias->GetTotalQuantity(), 999);
	TestEqual(TEXT("Legacy object slot is not mutated"), Alias->GetSlotIndex(), 2);
	TestTrue(TEXT("Callbacks do not populate a second inventory"), Fixture.Legacy->GetAllItems().IsEmpty());
	FRPGItemRecord After;
	const IRPGItemRecordSource* Records = Fixture.Projection->GetAuthoritativeRecordSource();
	if (!TestTrue(TEXT("Native record remains available"), Records && Records->Find(
		Fixture.Record.GetItemId(), After))) return false;
	TestTrue(TEXT("Retired writes leave the complete native record unchanged"),
		FRPGItemRecord::StaticStruct()->CompareScriptStruct(&Fixture.Record, &After, 0));
	TestFalse(TEXT("Unsupported writes do not submit native commands"), Fixture.Model->bHasLastCommandResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLegacyInventoryConsumeRetirementTest,
	"ProjectRPG.Item.LegacyRetirement.ConsumeUsesOnlyExistingCanonicalId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGLegacyInventoryConsumeRetirementTest::RunTest(const FString&)
{
	using namespace RPGLegacyInventoryRetirementTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Legacy compatibility facade"), Fixture.Legacy)) return false;
	Fixture.Legacy->Server_ConsumeItem(nullptr);
	URPGItemBase* Unknown = Fixture.MakeAlias(FGuid::NewGuid());
	Fixture.Legacy->Server_ConsumeItem(Unknown);
	TestFalse(TEXT("Unmigrated item never creates or submits a native item"),
		Fixture.Model->bHasLastCommandResult);
	TestEqual(TEXT("Unmigrated object is preserved"), Unknown->GetTotalQuantity(), 999);
	URPGItemBase* Alias = Fixture.MakeAlias(Fixture.Record.GetItemId());
	Fixture.Legacy->Server_ConsumeItem(Alias);
	TestTrue(TEXT("Existing native ID submits through the facade"), Fixture.Model->bHasLastCommandResult);
	TestEqual(TEXT("Submission is a native consume operation"),
		Fixture.Model->LastCommandResult.Operation, FName(TEXT("Item.Consume")));
	// The fixture has no authenticated PlayerState or backend. The native facade rejects,
	// proving that legacy calls cannot run their old immediate gameplay/saving path.
	TestEqual(TEXT("Native server gates remain in force"), Fixture.Model->LastCommandResult.Result,
		ERPGItemCommandResultCode::ServerStateError);
	TestEqual(TEXT("Forwarding does not trust or debit legacy quantity"), Alias->GetTotalQuantity(), 999);
	FRPGItemRecord After;
	const IRPGItemRecordSource* Records = Fixture.Projection->GetAuthoritativeRecordSource();
	TestTrue(TEXT("Rejected native consume preserves record"), Records && Records->Find(
		Fixture.Record.GetItemId(), After)
		&& FRPGItemRecord::StaticStruct()->CompareScriptStruct(&Fixture.Record, &After, 0));
	TestTrue(TEXT("No legacy list is reconstructed"), Fixture.Legacy->GetAllItems().IsEmpty());
	return true;
}

#endif
