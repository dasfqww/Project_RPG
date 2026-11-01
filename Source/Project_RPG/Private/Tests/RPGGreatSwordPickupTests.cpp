#if WITH_DEV_AUTOMATION_TESTS

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/RPGPickupGrantComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Equipment/RPGEquipmentSnapshot.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Item/Persistence/RPGInMemoryItemRepository.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"
#include "Item/Transaction/RPGItemPickupGrantPlanner.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "Engine/StaticMesh.h"
#include "Player/RPGPlayerState.h"

namespace RPGGreatSwordPickupTests
{
const TCHAR* DefinitionPath = TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword");

FRPGItemBackendCommitResult ToReceipt(const FRPGItemRepositoryCommitResult& Result)
{
	FRPGItemBackendCommitResult Receipt;
	Receipt.Status = Result.Status == ERPGItemRepositoryCommitStatus::AlreadyCommitted
		? ERPGItemBackendStatus::AlreadyApplied
		: Result.Status == ERPGItemRepositoryCommitStatus::Committed
			? ERPGItemBackendStatus::Succeeded : ERPGItemBackendStatus::ServerError;
	Receipt.RequestId = Result.RequestId;
	Receipt.Operation = Result.Operation;
	Receipt.CommandFingerprint = Result.CommandFingerprint;
	Receipt.Actor = Result.Actor;
	Receipt.AffectedQuantity = Result.AffectedQuantity;
	Receipt.Records = Result.Records;
	return Receipt;
}

// In-memory test boundary only: no HTTP transport, authenticated session, or real DB write.
class FDeferredRepositoryCommitter final : public IRPGItemAsyncCommitter
{
public:
	FRPGInMemoryItemRepository Repository;
	FRPGItemRepositoryCommitRequest Frozen;
	FRPGItemBackendCommitCompletion Pending;
	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) override
	{
		Frozen = Request;
		Pending = MoveTemp(Completion);
		return true;
	}
	void Reply()
	{
		auto Callback = MoveTemp(Pending);
		Callback(ToReceipt(Repository.Commit(Frozen)));
	}
};

struct FFixture
{
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	ARPGPickUpBase* Pickup = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGItemCommandComponent* Commands = nullptr;
	FRPGItemOwnerRef Owner;
	FFixture(UClass* PickupClass)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World || !PickupClass) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>();
		auto* State = World->SpawnActor<ARPGPlayerState>();
		Pickup = World->SpawnActor<ARPGPickUpBase>(PickupClass);
		if (!Controller || !State || !Pickup) return;
		Owner.Type = ERPGItemOwnerType::Character;
		Owner.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
		Controller->SetPlayerState(State);
		State->SetAuthenticatedIdentity(Owner.OwnerId, TEXT("76561198000000001"), TEXT("pickup-content-test"));
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection);
		Projection->RegisterComponent();
		Projection->ApplyAuthoritativeRecords(Owner, {});
		Commands = NewObject<URPGItemCommandComponent>(Controller);
		Controller->AddInstanceComponent(Commands);
		Commands->RegisterComponent();
	}
	~FFixture()
	{
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGreatSwordPickupContentTest,
	"ProjectRPG.Item.GreatSwordPickup.AuthoredContentAndInteractionQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGGreatSwordPickupContentTest::RunTest(const FString&)
{
	using namespace RPGGreatSwordPickupTests;
	const auto* Definition = LoadObject<URPGItemDefinition>(nullptr, DefinitionPath);
	if (!TestNotNull(TEXT("Authored native GreatSword definition"), Definition)) return false;
	UClass* PickupClass = Definition->PickupActorClass.LoadSynchronous();
	if (!TestTrue(TEXT("Definition points to the native pickup facade"),
		PickupClass && PickupClass->IsChildOf(ARPGPickUpBase::StaticClass()))) return false;
	TestEqual(TEXT("Neutral pickup path, no V2 suffix"), PickupClass->GetPathName(),
		FString(TEXT("/Game/Blueprints/Item/Equipment/BP_Pickup_GreatSword.BP_Pickup_GreatSword_C")));
	const auto* Equipment = Definition->FindFragmentByClass<URPGItemEquipmentDefinitionFragment>();
	if (!TestNotNull(TEXT("Authored equipment fragment"), Equipment)) return false;
	UClass* WeaponClass = Equipment->EquippedActorClass.LoadSynchronous();
	if (!TestTrue(TEXT("Existing weapon actor is reused"),
		WeaponClass && WeaponClass->IsChildOf(ARPGPlayerWeapon::StaticClass()))) return false;
	const auto* Weapon = CastChecked<ARPGPlayerWeapon>(WeaponClass->GetDefaultObject());
	TestTrue(TEXT("Pickup mesh comes from the actual equipped actor"),
		Definition->PickupMesh.LoadSynchronous() == Weapon->GetWeaponMesh()->GetStaticMesh());
	TestTrue(TEXT("Generic equipment grants remain empty to avoid duplicate active abilities"), Equipment->GrantedAbilitySets.IsEmpty());
	TestEqual(TEXT("Weapon is a non-stackable single item"), Definition->GetMaxStackSize(), 1);
	FFixture F(PickupClass);
	if (!TestNotNull(TEXT("Authored pickup spawned"), F.Pickup)) return false;
	const auto Manifest = F.Pickup->GetItemManifest();
	TestEqual(TEXT("Pickup identity selects the native definition"),
		URPGItemDefinition::MakePrimaryAssetIdForTag(Manifest.GetItemTag()), Definition->GetPrimaryAssetId());
	TestEqual(TEXT("Pickup category agrees with native definition"), Manifest.GetItemCategory(), Definition->ItemCategory);
	TestTrue(TEXT("No legacy stats or fragments copied into pickup"), Manifest.GetAllFragmentsOfType<FItemFragment>().IsEmpty());
	TestNull(TEXT("No second mutable URPGItemBase created"), F.Pickup->GetRPGItemData());
	TestTrue(TEXT("Pickup replicates"), F.Pickup->GetIsReplicated());
	const auto* Mesh = Cast<UStaticMeshComponent>(F.Pickup->GetRootComponent());
	if (!TestNotNull(TEXT("Pickup static mesh"), Mesh)) return false;
	TestEqual(TEXT("Existing mesh scale is preserved"), Mesh->GetRelativeScale3D(), Weapon->GetWeaponMesh()->GetRelativeScale3D());
	TestEqual(TEXT("Pickup is a WorldDynamic interaction target"), Mesh->GetCollisionObjectType(), ECC_WorldDynamic);
	TestEqual(TEXT("Pickup uses query-only collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("Pickup does not simulate physics"), Mesh->IsSimulatingPhysics());
	F.Pickup->DispatchBeginPlay();
	TestEqual(TEXT("Native pickup interaction name is populated"), F.Pickup->InteractableData.Name.ToString(), Definition->DisplayName.ToString());
	TestEqual(TEXT("Native pickup interaction quantity is one"), static_cast<int32>(F.Pickup->InteractableData.Quantity), 1);
	TestFalse(TEXT("Interaction action is populated"), F.Pickup->InteractableData.Action.IsEmpty());
	TArray<FOverlapResult> Hits;
	F.World->OverlapMultiByObjectType(Hits, Mesh->Bounds.Origin, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(100.f));
	TestTrue(TEXT("Existing WorldDynamic overlap query discovers the authored pickup"),
		Hits.ContainsByPredicate([&F](const FOverlapResult& Hit) { return Hit.GetActor() == F.Pickup; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGreatSwordPickupIdentityTest,
	"ProjectRPG.Item.GreatSwordPickup.ReceiptEquipUnequipPreservesIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGGreatSwordPickupIdentityTest::RunTest(const FString&)
{
	using namespace RPGGreatSwordPickupTests;
	const auto* Definition = LoadObject<URPGItemDefinition>(nullptr, DefinitionPath);
	if (!TestNotNull(TEXT("Native definition"), Definition)) return false;
	FFixture F(Definition->PickupActorClass.LoadSynchronous());
	if (!TestNotNull(TEXT("Projection fixture"), F.Commands) || !F.Pickup) return false;
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	if (!TestTrue(TEXT("Native definition registers"), Definitions.RegisterDefinition(*Definition)) ||
		!TestTrue(TEXT("Native equipment action policy registers"), Actions.RegisterDefinition(*Definition))) return false;
	const auto Committer = MakeShared<FDeferredRepositoryCommitter>();
	FRPGItemPickupGrantRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.ItemId = FGuid::NewGuid();
	Request.Owner = F.Owner;
	Request.GenerationSeed = 12345;
	Request.Quantity = 1; // Non-stackable pickup has no legacy quantity fragment.
	Request.CreationSource = TEXT("WorldPickup");
	FRPGItemRepositoryCommitRequest GrantPlan;
	if (!TestTrue(TEXT("Actual GreatSword grant plans"), FRPGItemPickupGrantPlanner::TryPlan(
		Request, *Definition, Committer->Repository, Definitions, GrantPlan))) return false;
	auto* Grant = F.Pickup->FindComponentByClass<URPGPickupGrantComponent>();
	if (!TestNotNull(TEXT("Pickup owns grant lifetime"), Grant) ||
		!TestTrue(TEXT("Grant starts on server"), Grant->BeginGrant(GrantPlan, *F.Commands, Committer))) return false;
	TestEqual(TEXT("No optimistic inventory entry before receipt"), F.Projection->GetProjectedItems().Num(), 0);
	TestFalse(TEXT("Concurrent claimant cannot mint another sword"), F.Pickup->TryClaimPickup());
	Committer->Reply();
	FRPGInventoryProjectionEntry Entry;
	if (!TestTrue(TEXT("Accepted pickup receipt is projected"), F.Projection->FindProjectedItem(Request.ItemId, Entry))) return false;
	TestEqual(TEXT("Acquired item starts at revision one"), Entry.GetRevision(), int64(1));
	TestEqual(TEXT("Acquired definition identity matches"), Entry.GetDefinitionId(), Definition->GetPrimaryAssetId());
	TestTrue(TEXT("Accepted pickup retires"), F.Pickup->IsActorBeingDestroyed());
	TestFalse(TEXT("No retry remains"), Grant->IsPending());
	TestTrue(TEXT("Grant replay returns the same durable receipt"),
		Committer->Repository.Commit(GrantPlan).Status == ERPGItemRepositoryCommitStatus::AlreadyCommitted);
	FRPGItemActionCommandPlanner Planner(Committer->Repository, Definitions, Actions);
	FRPGItemEquipRequest Equip;
	Equip.RequestId = FGuid::NewGuid();
	Equip.Actor = F.Owner;
	Equip.ItemId = Request.ItemId;
	Equip.ExpectedRevision = Entry.GetRevision();
	Equip.EquipmentContainerId = F.Owner.OwnerId;
	Equip.SlotType = EEquipmentSlotType::Weapon_Primary_L;
	TestFalse(TEXT("Unauthored left-hand slot is rejected"), Planner.Prepare(Equip).IsReady());
	Equip.SlotType = EEquipmentSlotType::Weapon_Primary_R;
	const auto EquipPlan = Planner.Prepare(Equip);
	if (!TestTrue(TEXT("Acquired sword can equip in authored right slot"), EquipPlan.IsReady())) return false;
	const auto Equipped = Committer->Repository.Commit(EquipPlan.CommitRequest);
	if (!TestTrue(TEXT("Equip commits atomically"), Equipped.WasApplied()) ||
		!TestTrue(TEXT("Equip receipt updates owner projection"),
			F.Projection->ApplyAuthoritativeCommitResult(F.Owner, ToReceipt(Equipped)))) return false;
	F.Projection->FindProjectedItem(Request.ItemId, Entry);
	TestEqual(TEXT("Same acquired item is projected as equipment"), Entry.GetContainerType(), ERPGItemContainerType::Equipment);
	TestEqual(TEXT("Equip increases revision without replacing identity"), Entry.GetRevision(), int64(2));
	if (!TestEqual(TEXT("One equipped record"), Equipped.Records.Num(), 1)) return false;
	FRPGEquipmentSnapshot Snapshot;
	TestTrue(TEXT("Actual native equipment snapshot builds"), FRPGEquipmentSnapshot::TryBuild(Equipped.Records[0], *Definition, Snapshot));
	TestEqual(TEXT("Equipment snapshot retains pickup identity"), Snapshot.ItemId, Request.ItemId);
	TestEqual(TEXT("Snapshot retains GreatSword type"), Snapshot.WeaponType, ERPGGladiatorWeaponType::GreatSword);
	TestFalse(TEXT("Stale equip revision cannot be reused"), Planner.Prepare(Equip).IsReady());
	FRPGItemUnequipRequest Unequip;
	Unequip.RequestId = FGuid::NewGuid();
	Unequip.Actor = F.Owner;
	Unequip.ItemId = Request.ItemId;
	Unequip.ExpectedRevision = Entry.GetRevision();
	Unequip.InventoryDestination.ContainerType = ERPGItemContainerType::Inventory;
	Unequip.InventoryDestination.ContainerId = F.Owner.OwnerId;
	Unequip.InventoryDestination.SlotIndex = 0;
	const auto UnequipPlan = Planner.Prepare(Unequip);
	if (!TestTrue(TEXT("Same sword can return to inventory"), UnequipPlan.IsReady())) return false;
	const auto Returned = Committer->Repository.Commit(UnequipPlan.CommitRequest);
	if (!TestTrue(TEXT("Unequip commits"), Returned.WasApplied()) ||
		!TestTrue(TEXT("Unequip receipt updates projection"), F.Projection->ApplyAuthoritativeCommitResult(F.Owner, ToReceipt(Returned)))) return false;
	F.Projection->FindProjectedItem(Request.ItemId, Entry);
	TestEqual(TEXT("Original identity returns at revision three"), Entry.GetRevision(), int64(3));
	TestEqual(TEXT("Returned entry is inventory, not another grant"), Entry.GetContainerType(), ERPGItemContainerType::Inventory);
	TestEqual(TEXT("Only one item remains projected"), F.Projection->GetProjectedItems().Num(), 1);
	TArray<FRPGItemRecord> Records;
	Committer->Repository.FindByOwner(F.Owner, Records);
	TestEqual(TEXT("Only one durable test item exists after grant replay and movement"), Records.Num(), 1);
	return true;
}

#endif
