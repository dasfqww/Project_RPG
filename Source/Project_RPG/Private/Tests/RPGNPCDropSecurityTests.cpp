#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Ability/NPC/RPGNPCDropItemAbility.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "DataTable/DropItemData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "RPGItemTags.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace RPGNPCDropTests
{
struct FFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
	ARPGNonPlayerCharacter* NPC = nullptr;
	URPGAbilitySystemComponent* ASC = nullptr;
	FFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		NPC = World->SpawnActor<ARPGNonPlayerCharacter>();
		ASC = NPC->GetRPGAbilitySystemComponent();
		ASC->InitAbilityActorInfo(NPC, NPC);
		if (!ASC->GetSet<URPGAttributeSet>()) ASC->AddAttributeSetSubobject(NPC->GetRPGAttributeSet());
		NPC->GetRPGAttributeSet()->SetCurrentHealth(10.f);
	}
	~FFixture()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	void Dead()
	{
		// Native fixture only, not evidence of natural combat or death BP replication.
		NPC->GetRPGAttributeSet()->SetCurrentHealth(0.f);
		ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
	}
	URPGNPCDropItemAbility* Give(UClass* Class = URPGNPCDropItemAbility::StaticClass())
	{
		const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
		return Cast<URPGNPCDropItemAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	}
	int32 Pickups() const
	{
		int32 Count = 0;
		for (TActorIterator<ARPGPickUpBase> It(World); It; ++It) if (IsValid(*It)) ++Count;
		return Count;
	}
};
void Configure(URPGNPCDropItemAbility& Ability, UDataTable* Table, FName Row = TEXT("TestDrop"))
{
	FindFProperty<FObjectPropertyBase>(Ability.GetClass(), TEXT("ItemDropTable"))->SetObjectPropertyValue_InContainer(&Ability, Table);
	FindFProperty<FNameProperty>(Ability.GetClass(), TEXT("DropRowName"))->SetPropertyValue_InContainer(&Ability, Row);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNPCDeathDropGateTest,
	"ProjectRPG.Item.NPCDrop.ServerDeathAndActorLifetimeGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNPCDeathDropGateTest::RunTest(const FString&)
{
	using namespace RPGNPCDropTests;
	FFixture F;
	TestFalse(TEXT("Living NPC cannot drop"), F.NPC->TryBeginDeathItemDrop());
	F.NPC->GetRPGAttributeSet()->SetCurrentHealth(0.f);
	TestFalse(TEXT("Zero HP without actual death tag is insufficient"), F.NPC->TryBeginDeathItemDrop());
	F.ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
	F.NPC->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client role cannot consume the drop claim"), F.NPC->TryBeginDeathItemDrop());
	F.NPC->SetRole(ROLE_Authority);
	F.NPC->GetRPGAttributeSet()->InitCurrentHealth(std::numeric_limits<float>::quiet_NaN());
	TestFalse(TEXT("Invalid HP cannot consume the drop claim"), F.NPC->TryBeginDeathItemDrop());
	F.NPC->GetRPGAttributeSet()->InitCurrentHealth(-1.f);
	TestFalse(TEXT("Negative HP cannot consume the drop claim"), F.NPC->TryBeginDeathItemDrop());
	F.NPC->GetRPGAttributeSet()->SetCurrentHealth(0.f);
	F.ASC->InitAbilityActorInfo(F.NPC, F.World->SpawnActor<AActor>());
	TestFalse(TEXT("Detached ASC avatar cannot consume the claim"), F.NPC->TryBeginDeathItemDrop());
	F.ASC->InitAbilityActorInfo(F.NPC, F.NPC);
	TestTrue(TEXT("First authoritative death claims once"), F.NPC->TryBeginDeathItemDrop());
	TestFalse(TEXT("Repeated callback cannot claim again"), F.NPC->TryBeginDeathItemDrop());
	F.ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
	F.NPC->GetRPGAttributeSet()->SetCurrentHealth(10.f);
	F.Dead();
	TestFalse(TEXT("HP/tag toggles do not reset the actor-lifetime claim"), F.NPC->TryBeginDeathItemDrop());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNPCDropTableAndReplayTest,
	"ProjectRPG.Item.NPCDrop.TableValidationAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNPCDropTableAndReplayTest::RunTest(const FString&)
{
	using namespace RPGNPCDropTests;
	FFixture F;
	F.Dead();
	auto* Ability = F.Give();
	if (!TestNotNull(TEXT("Instanced native drop ability"), Ability)) return false;
	F.ASC->TryActivateAbilityByClass(Ability->GetClass());
	TestEqual(TEXT("Null table is rejected without spawning"), F.Pickups(), 0);
	TestTrue(TEXT("Invalid configuration has not consumed the claim"), F.NPC->CanProcessDeathItemDrop());
	auto* Table = NewObject<UDataTable>(Ability);
	Table->RowStruct = FItemDropTable::StaticStruct();
	Configure(*Ability, Table);
	F.ASC->TryActivateAbilityByClass(Ability->GetClass());
	TestEqual(TEXT("Missing row is rejected without spawning"), F.Pickups(), 0);
	Table->RowStruct = FTableRowBase::StaticStruct();
	F.ASC->TryActivateAbilityByClass(Ability->GetClass());
	TestEqual(TEXT("Wrong row type rejected before FindRow"), F.Pickups(), 0);
	Table->RowStruct = FItemDropTable::StaticStruct();
	FItemDropTable Oversized;
	Oversized.DropItemList.SetNum(33);
	Table->AddRow(TEXT("TestDrop"), Oversized);
	F.ASC->TryActivateAbilityByClass(Ability->GetClass());
	TestTrue(TEXT("Overlarge row rejected before consuming the claim"), F.NPC->CanProcessDeathItemDrop());
	FItemDropTable Row;
	FDropItem& Drop = Row.DropItemList.AddDefaulted_GetRef();
	Drop.ItemClass = LoadClass<ARPGPickUpBase>(nullptr,
		TEXT("/Game/Blueprints/Item/Equipment/BP_Pickup_GreatSword.BP_Pickup_GreatSword_C"));
	if (!TestNotNull(TEXT("Saved pickup BP"), Drop.ItemClass.Get())) return false;
	Drop.DropQuantity = 1;
	Drop.DropChance = std::numeric_limits<float>::quiet_NaN();
	Table->AddRow(TEXT("TestDrop"), Row);
	F.ASC->TryActivateAbilityByClass(Ability->GetClass());
	TestEqual(TEXT("NaN chance is rejected"), F.Pickups(), 0);
	Drop.DropChance = 1.f;
	Drop.DropQuantity = 2;
	Table->AddRow(TEXT("TestDrop"), Row);
	F.ASC->TryActivateAbilityByClass(Ability->GetClass());
	TestEqual(TEXT("Non-stackable quantity cannot be silently reduced"), F.Pickups(), 0);
	Drop.DropQuantity = 1;
	Table->AddRow(TEXT("TestDrop"), Row);
	int32 Reentries = 0;
	const auto ReentryHandle = F.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
		[Ability, &Reentries](AActor* Actor)
		{
			if (Actor->IsA<ARPGPickUpBase>()) { ++Reentries; Ability->SpawnItem(); }
		}));
	TestTrue(TEXT("Valid drop ability activates"), F.ASC->TryActivateAbilityByClass(Ability->GetClass()));
	F.World->RemoveOnActorSpawnedHandler(ReentryHandle);
	TestEqual(TEXT("Actual spawn callback attempted reentry once"), Reentries, 1);
	TestEqual(TEXT("Guaranteed transient row spawns one actual saved pickup"), F.Pickups(), 1);
	TestFalse(TEXT("Drop activation ends rather than staying active"), F.ASC->FindAbilitySpecFromClass(Ability->GetClass())->IsActive());
	TestFalse(TEXT("Repeated activation cannot reroll drops"), F.ASC->TryActivateAbilityByClass(Ability->GetClass()));
	Ability->SpawnItem();
	TestEqual(TEXT("Direct repeated spawn is also blocked"), F.Pickups(), 1);
	F.Give();
	TestFalse(TEXT("Another ability spec cannot bypass the NPC claim"), F.ASC->TryActivateAbilityByClass(Ability->GetClass()));
	TestEqual(TEXT("No duplicate pickup after another spec"), F.Pickups(), 1);
	FFixture Zero;
	Zero.Dead();
	auto* ZeroAbility = Zero.Give();
	auto* ZeroTable = NewObject<UDataTable>(ZeroAbility);
	ZeroTable->RowStruct = FItemDropTable::StaticStruct();
	FItemDropTable Disabled;
	Disabled.DropItemList.AddDefaulted(); // Exact zero chance, no class or reward invented.
	ZeroTable->AddRow(TEXT("TestDrop"), Disabled);
	Configure(*ZeroAbility, ZeroTable);
	Zero.ASC->TryActivateAbilityByClass(ZeroAbility->GetClass());
	TestEqual(TEXT("Zero probability never creates a drop"), Zero.Pickups(), 0);
	TestFalse(TEXT("No-drop outcome cannot be rerolled"), Zero.NPC->CanProcessDeathItemDrop());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNPCDropQuantityTest,
	"ProjectRPG.Item.NPCDrop.ValidatedQuantityPreservesManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNPCDropQuantityTest::RunTest(const FString&)
{
	using namespace RPGNPCDropTests;
	FFixture F;
	auto* Pickup = F.World->SpawnActor<ARPGPickUpBase>();
	TestFalse(TEXT("Unidentified pickup cannot initialize a drop"), Pickup->InitializeServerDropQuantity(3));
	FItemManifest Manifest;
	*FindFProperty<FStructProperty>(FItemManifest::StaticStruct(), TEXT("ItemTag"))->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = RPGGameplayTags::GameItem_Craft_fruit;
	*FindFProperty<FEnumProperty>(FItemManifest::StaticStruct(), TEXT("ItemCategory"))->ContainerPtrToValuePtr<EItemCategory>(&Manifest) = EItemCategory::Craft;
	Manifest.GetFragmentsMutable().Add(TInstancedStruct<FItemFragment>::Make<FStackableFragment>());
	FindFProperty<FIntProperty>(FStackableFragment::StaticStruct(), TEXT("MaxQuantity"))->SetPropertyValue_InContainer(Manifest.GetFragmentOfTypeMutable<FStackableFragment>(), 10);
	Pickup->InitItemManifest(Manifest);
	Pickup->InteractableData.Name = FText::FromString(TEXT("Preserved name"));
	TestFalse(TEXT("Zero quantity rejected"), Pickup->InitializeServerDropQuantity(0));
	TestFalse(TEXT("Over-stack quantity rejected"), Pickup->InitializeServerDropQuantity(11));
	TestTrue(TEXT("Valid stack quantity initialized"), Pickup->InitializeServerDropQuantity(3));
	TestEqual(TEXT("Manifest is the source of pickup grant quantity"), Pickup->GetItemManifest().GetFragmentOfType<FStackableFragment>()->GetQuantity(), 3);
	TestEqual(TEXT("Interaction quantity agrees"), static_cast<int32>(Pickup->InteractableData.Quantity), 3);
	TestEqual(TEXT("Existing display name survives initialization"), Pickup->InteractableData.Name.ToString(), FString(TEXT("Preserved name")));
	Pickup->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot rewrite quantity"), Pickup->InitializeServerDropQuantity(4));
	Pickup->SetRole(ROLE_Authority);
	TestTrue(TEXT("Pickup can be claimed by the existing grant path"), Pickup->TryClaimPickup());
	TestFalse(TEXT("Claimed pickup cannot be rewritten"), Pickup->InitializeServerDropQuantity(4));
	TestEqual(TEXT("Rejected rewrite leaves quantity unchanged"), Pickup->GetItemManifest().GetFragmentOfType<FStackableFragment>()->GetQuantity(), 3);
	// Even a larger manifest stack limit cannot overflow the saved int8 BP display.
	auto* LargePickup = F.World->SpawnActor<ARPGPickUpBase>();
	FindFProperty<FIntProperty>(FStackableFragment::StaticStruct(), TEXT("MaxQuantity"))->SetPropertyValue_InContainer(Manifest.GetFragmentOfTypeMutable<FStackableFragment>(), 1000);
	LargePickup->InitItemManifest(Manifest);
	TestFalse(TEXT("Quantity 128 cannot wrap the legacy BP interaction display"), LargePickup->InitializeServerDropQuantity(128));
	TestTrue(TEXT("Largest representable interaction quantity remains valid"), LargePickup->InitializeServerDropQuantity(127));
	TestEqual(TEXT("Boundary quantity preserves manifest/display agreement"), static_cast<int32>(LargePickup->InteractableData.Quantity), 127);
	TestEqual(TEXT("Boundary manifest quantity is not truncated"), LargePickup->GetItemManifest().GetFragmentOfType<FStackableFragment>()->GetQuantity(), 127);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNPCSavedDropPolicyTest,
	"ProjectRPG.Item.NPCDrop.AuthoredGuardianServerOnlyPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNPCSavedDropPolicyTest::RunTest(const FString&)
{
	UClass* Class = LoadClass<URPGNPCDropItemAbility>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Guardian_DropItemAbility.BP_Guardian_DropItemAbility_C"));
	if (!TestNotNull(TEXT("Saved Guardian drop BP"), Class)) return false;
	const auto* Defaults = Class->GetDefaultObject<URPGNPCDropItemAbility>();
	TestEqual(TEXT("Saved BP executes only on server"), Defaults->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerOnly);
	TestEqual(TEXT("Saved BP rejects client activation/termination"), Defaults->GetNetSecurityPolicy(), EGameplayAbilityNetSecurityPolicy::ServerOnly);
	const auto* Table = Cast<UDataTable>(FindFProperty<FObjectPropertyBase>(Class, TEXT("ItemDropTable"))->GetObjectPropertyValue_InContainer(Defaults));
	if (!TestNotNull(TEXT("Existing table binding preserved"), Table)) return false;
	const FName RowName = FindFProperty<FNameProperty>(Class, TEXT("DropRowName"))->GetPropertyValue_InContainer(Defaults);
	TestEqual(TEXT("Guardian row binding preserved"), RowName, FName(TEXT("Guardian")));
	const auto* Row = Table->FindRow<FItemDropTable>(RowName, TEXT("Saved NPC drop policy test"), false);
	TestNotNull(TEXT("Saved row exists; reward contents are not filled by the test"), Row);
	return true;
}
#endif
