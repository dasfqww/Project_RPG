#if WITH_DEV_AUTOMATION_TESTS

#include "Economy/RPGDungeonRewardDefinition.h"
#include "Online/RPGDungeonServerDefinition.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace RPGDungeonPersonalLootTests
{
	const TCHAR* const Names[] = {
		TEXT("GameItem.Material.Progression.Ore"), TEXT("GameItem.Material.Progression.Token"),
		TEXT("GameItem.Equipment.Progression.Target.EnduranceGreatSword"),
		TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"),
		TEXT("GameItem.Equipment.Progression.Target.Chest"), TEXT("GameItem.Equipment.Progression.Target.Gloves")
	};

	URPGDungeonRewardDefinition* MakeDefinition()
	{
		auto* Definition = NewObject<URPGDungeonRewardDefinition>();
		Definition->RewardVersion = TEXT("FirstSlice.v1");
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			auto* Item = NewObject<URPGItemDefinition>(Definition);
			UGameplayTagsManager::Get().ImportSingleGameplayTag(Item->ItemTag, FName(Names[Index]), true);
			Item->MaxStackSize = Index < 2 ? 999 : 1;
			auto& Entry = Definition->ItemRewards.AddDefaulted_GetRef();
			Entry.ItemDefinition = Item;
			Entry.Quantity = Index == 0 ? 4 : Index == 1 ? 2 : 1;
			Entry.BindState = ERPGItemBindState::CharacterBound;
			Entry.PersonalLootWeight = Index < 2 ? 0 : 500;
			Entry.bIncludeEquipmentProgression = Index >= 2;
		}
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPersonalLootWireTest,
	"ProjectRPG.Online.DungeonReward.PersonalLoot.Wire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPersonalLootWireTest::RunTest(const FString&)
{
	FRPGDungeonItemReward Reward;
	FJsonObject Object; Object.SetNumberField(TEXT("quantity"), 4);
	Object.SetNumberField(TEXT("personalLootWeight"), 500);
	TestTrue(TEXT("Default is guaranteed, including legacy stack zero"), Reward.WritePersonalLootWeight(Object));
	TestFalse(TEXT("Zero is omitted, not invalid backend numeric zero"), Object.HasField(TEXT("personalLootWeight")));
	TestEqual(TEXT("Other payload data is preserved"), Object.GetIntegerField(TEXT("quantity")), 4);
	Reward.MaxStackSize = 1;
	for (int32 Weight : { 1, 500, 10000 })
	{
		Reward.PersonalLootWeight = Weight;
		TestTrue(TEXT("Positive basis points serialize"), Reward.WritePersonalLootWeight(Object));
		TestEqual(TEXT("Wire weight remains exact"), Object.GetIntegerField(TEXT("personalLootWeight")), Weight);
	}
	for (int32 Weight : { -1, 10001, MAX_int32 })
	{
		Reward.PersonalLootWeight = Weight;
		TestFalse(TEXT("Out-of-range writes fail closed"), Reward.WritePersonalLootWeight(Object));
		TestEqual(TEXT("Rejected write does not mutate existing JSON"), Object.GetIntegerField(TEXT("personalLootWeight")), 10000);
	}
	Reward.PersonalLootWeight = 500; Reward.MaxStackSize = 0;
	TestFalse(TEXT("Weighted mail-only legacy entry is forbidden"), Reward.WritePersonalLootWeight(Object));
	Reward.MaxStackSize = 1;
	TArray<FRPGDungeonItemReward> Pool; Pool.Init(Reward, 4);
	TestTrue(TEXT("Four 5% entries form the 20% exclusive pool"), FRPGDungeonItemReward::HasValidPersonalLootPool(Pool));
	for (auto& Entry : Pool) Entry.PersonalLootWeight = 2500;
	TestTrue(TEXT("Exactly 100% is valid"), FRPGDungeonItemReward::HasValidPersonalLootPool(Pool));
	Pool[0].PersonalLootWeight++;
	TestFalse(TEXT("Pool sum overflow cannot be sent"), FRPGDungeonItemReward::HasValidPersonalLootPool(Pool));
	Pool[0].PersonalLootWeight = MIN_int32;
	TestFalse(TEXT("Negative weight cannot cancel the sum"), FRPGDungeonItemReward::HasValidPersonalLootPool(Pool));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPersonalLootAuthoringTest,
	"ProjectRPG.Online.DungeonReward.PersonalLoot.Authoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPersonalLootAuthoringTest::RunTest(const FString&)
{
	using namespace RPGDungeonPersonalLootTests;
	auto* Definition = MakeDefinition();
	FString Version, Error; TArray<FRPGCurrencyChange> Currency; TArray<FRPGDungeonItemReward> Items;
	if (!TestTrue(TEXT("FirstBoss authored table builds"), Definition->BuildSettlement(Version, Currency, Items, Error))) return false;
	if (!TestEqual(TEXT("Two fixed materials and four optional equipment entries"), Items.Num(), 6)) return false;
	TestEqual(TEXT("Progression version retained"), Version, FString(TEXT("FirstSlice.v1")));
	TestEqual(TEXT("Ore quantity remains guaranteed"), Items[0].Quantity, 4);
	TestEqual(TEXT("Token quantity remains guaranteed"), Items[1].Quantity, 2);
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		TestEqual(TEXT("Authoring carries the exact weight"), Items[Index].PersonalLootWeight, Index < 2 ? 0 : 500);
		TestEqual(TEXT("Only equipment opts into explicit +0"), Items[Index].EquipmentProgression.IsSet(), Index >= 2);
		FJsonObject Body;
		TestTrue(TEXT("Growth and loot codecs compose"), Items[Index].WriteEquipmentProgression(Body) && Items[Index].WritePersonalLootWeight(Body));
		TestEqual(TEXT("Fixed entries omit the loot field"), Body.HasField(TEXT("personalLootWeight")), Index >= 2);
	}
	for (int32 Weight : { -1, 10001, 9000 })
	{
		Definition->ItemRewards[2].PersonalLootWeight = Weight;
		TestFalse(TEXT("Invalid individual or aggregate weights reject authoring"), Definition->BuildSettlement(Version, Currency, Items, Error));
		TestTrue(TEXT("Rejected build clears all previous output"), Version.IsEmpty() && Currency.IsEmpty() && Items.IsEmpty() && !Error.IsEmpty());
	}
	for (auto& Entry : Definition->ItemRewards) Entry.PersonalLootWeight = 0;
	if (!TestTrue(TEXT("Omitting all weights restores old guaranteed behavior"), Definition->BuildSettlement(Version, Currency, Items, Error))) return false;
	for (const auto& Item : Items) TestEqual(TEXT("No stale weight leaks from earlier builds"), Item.PersonalLootWeight, 0);
#if WITH_EDITOR
	const auto* Field = FindFProperty<FIntProperty>(FRPGDungeonItemRewardEntry::StaticStruct(), TEXT("PersonalLootWeight"));
	if (!TestNotNull(TEXT("UHT exposes the actual authoring field"), Field)) return false;
	TestTrue(TEXT("Authoring is editable and BP readable"), Field->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly));
	TestEqual(TEXT("Editor zero means guaranteed"), Field->GetMetaData(TEXT("ClampMin")), FString(TEXT("0")));
	TestEqual(TEXT("Editor uses basis points"), Field->GetMetaData(TEXT("ClampMax")), FString(TEXT("10000")));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPersonalLootSnapshotTest,
	"ProjectRPG.Online.DungeonReward.PersonalLoot.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPersonalLootSnapshotTest::RunTest(const FString&)
{
	auto* Reward = RPGDungeonPersonalLootTests::MakeDefinition();
	auto* Definition = NewObject<URPGDungeonServerDefinition>();
	Definition->DungeonId = TEXT("Progression.FirstBoss");
	Definition->DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/testmap.testmap")));
	Definition->ClearRewardsByDifficulty.Add(ERPGGameDifficulty::Normal, Reward);
	TArray<TObjectPtr<URPGDungeonServerDefinition>> Definitions = { Definition };
	FRPGDungeonServerSelection Frozen, Next; FString Error;
	if (!TestTrue(TEXT("Actual assigned-content path snapshots the pool"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, Definition->DungeonId, TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Frozen, Error))) return false;
	Reward->ItemRewards[2].PersonalLootWeight = 800;
	Reward->ItemRewards[0].Quantity = 5;
	if (!TestTrue(TEXT("A later selection can use changed authoring"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, Definition->DungeonId, TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Next, Error))) return false;
	if (!TestTrue(TEXT("Both snapshots contain all six entries"), Frozen.ItemRewards.Num() == 6 && Next.ItemRewards.Num() == 6)) return false;
	TestEqual(TEXT("In-progress selection retains original weight"), Frozen.ItemRewards[2].PersonalLootWeight, 500);
	TestEqual(TEXT("In-progress selection retains guaranteed quantity"), Frozen.ItemRewards[0].Quantity, 4);
	TestEqual(TEXT("Only next selection uses changed weight"), Next.ItemRewards[2].PersonalLootWeight, 800);
	FJsonObject Body;
	TestTrue(TEXT("Frozen payload encodes unchanged"), Frozen.ItemRewards[2].WritePersonalLootWeight(Body));
	TestEqual(TEXT("Wire uses frozen basis points"), Body.GetIntegerField(TEXT("personalLootWeight")), 500);
	Reward->ItemRewards[2].PersonalLootWeight = -1;
	TestFalse(TEXT("Invalid future pool cannot become an assignment"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, Definition->DungeonId, TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Next, Error));
	TestTrue(TEXT("Failed selection clears candidate only"), Next.ItemRewards.IsEmpty() && Frozen.ItemRewards[2].PersonalLootWeight == 500);
	return true;
}
#endif
