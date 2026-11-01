#if WITH_DEV_AUTOMATION_TESTS

#include "Economy/RPGDungeonRewardDefinition.h"
#include "Online/RPGDungeonServerDefinition.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace RPGDungeonEquipmentRewardTests
{
	const TCHAR* const Names[] = {
		TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),
		TEXT("GameItem.Equipment.Progression.Starter.Chest"),
		TEXT("GameItem.Equipment.Progression.Starter.Gloves"),
		TEXT("GameItem.Equipment.Progression.Target.EnduranceGreatSword"),
		TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"),
		TEXT("GameItem.Equipment.Progression.Target.Chest"),
		TEXT("GameItem.Equipment.Progression.Target.Gloves")
	};

	FRPGDungeonItemReward MakeReward(const TCHAR* Name = Names[3], int32 Level = 0)
	{
		FRPGDungeonItemReward Reward;
		Reward.DefinitionType = TEXT("RPGItemDefinition");
		Reward.DefinitionName = Name;
		Reward.MaxStackSize = 1;
		Reward.BindState = ERPGItemBindState::CharacterBound;
		Reward.EquipmentProgression.GrowthBandId = TEXT("FirstSlice");
		Reward.EquipmentProgression.RuleVersion = 1;
		Reward.EquipmentProgression.Level = Level;
		return Reward;
	}

	URPGDungeonRewardDefinition* MakeDefinition()
	{
		auto* Item = NewObject<URPGItemDefinition>();
		// Serialized-name unit fixture only: does not register tags or install content.
		UGameplayTagsManager::Get().ImportSingleGameplayTag(Item->ItemTag, FName(Names[3]), true);
		Item->MaxStackSize = 1;
		auto* Definition = NewObject<URPGDungeonRewardDefinition>();
		Definition->RewardVersion = TEXT("equipment_fixture_v1");
		auto& Entry = Definition->ItemRewards.AddDefaulted_GetRef();
		Entry.ItemDefinition = Item;
		Entry.BindState = ERPGItemBindState::CharacterBound;
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonEquipmentRewardWireTest,
	"ProjectRPG.Online.DungeonReward.EquipmentProgression.Wire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonEquipmentRewardWireTest::RunTest(const FString&)
{
	using namespace RPGDungeonEquipmentRewardTests;
	for (const TCHAR* Name : Names)
		for (int32 Level = 0; Level <= 3; ++Level)
		{
			const auto Reward = MakeReward(Name, Level);
			FJsonObject Object;
			Object.SetNumberField(TEXT("quantity"), 1);
			if (!TestTrue(TEXT("All seven exact definitions and four levels encode"), Reward.WriteEquipmentProgression(Object))) return false;
			const TSharedPtr<FJsonObject>* Growth = nullptr;
			if (!TestTrue(TEXT("Opt-in writes an object, including explicit +0"), Object.TryGetObjectField(TEXT("equipmentProgression"), Growth))) return false;
			TestEqual(TEXT("Only the three required fields are written"), (*Growth)->Values.Num(), 3);
			TestEqual(TEXT("Band retained exactly"), (*Growth)->GetStringField(TEXT("growthBandId")), FString(TEXT("FirstSlice")));
			TestEqual(TEXT("Rule retained exactly"), (*Growth)->GetIntegerField(TEXT("ruleVersion")), 1);
			TestEqual(TEXT("Level retained exactly"), (*Growth)->GetIntegerField(TEXT("level")), Level);
			TestEqual(TEXT("Other payload fields untouched"), Object.GetIntegerField(TEXT("quantity")), 1);
		}
	auto Legacy = MakeReward(); Legacy.EquipmentProgression = {};
	FJsonObject LegacyObject;
	LegacyObject.SetObjectField(TEXT("equipmentProgression"), MakeShared<FJsonObject>());
	TestTrue(TEXT("Legacy/unset remains accepted"), Legacy.WriteEquipmentProgression(LegacyObject));
	TestFalse(TEXT("Unset is omitted, not null or +0, even when reusing an object"), LegacyObject.HasField(TEXT("equipmentProgression")));

	auto Reject = [this](FRPGDungeonItemReward Reward)
	{
		FJsonObject Object; Object.SetNumberField(TEXT("unchanged"), 19);
		TestFalse(TEXT("Invalid opt-in fails closed"), Reward.HasValidEquipmentProgression());
		TestFalse(TEXT("Invalid opt-in cannot reach the wire"), Reward.WriteEquipmentProgression(Object));
		TestTrue(TEXT("Rejected write is non-mutating"), Object.Values.Num() == 1 && Object.GetIntegerField(TEXT("unchanged")) == 19);
	};
	for (int32 Level : { -1, 4, MAX_int32 }) { auto R = MakeReward(); R.EquipmentProgression.Level = Level; Reject(R); }
	{ auto R = MakeReward(); R.EquipmentProgression.GrowthBandId = TEXT("firstslice"); Reject(R); }
	{ auto R = MakeReward(); R.EquipmentProgression.GrowthBandId.Reset(); Reject(R); }
	for (int32 Rule : { 0, 2 }) { auto R = MakeReward(); R.EquipmentProgression.RuleVersion = Rule; Reject(R); }
	{ auto R = MakeReward(); R.DefinitionVersion = 2; Reject(R); }
	{ auto R = MakeReward(); R.DefinitionType = TEXT("OtherDefinition"); Reject(R); }
	{ auto R = MakeReward(); R.DefinitionName = TEXT("GameItem.Equipment.Progression.Target.Unknown"); Reject(R); }
	{ auto R = MakeReward(); R.DefinitionName = TEXT("GameItem.Material.Progression.Ore"); Reject(R); }
	{ auto R = MakeReward(); R.Quantity = 2; Reject(R); }
	for (int32 Stack : { 0, 2 }) { auto R = MakeReward(); R.MaxStackSize = Stack; Reject(R); }
	for (auto Bind : { ERPGItemBindState::Unbound, ERPGItemBindState::BindOnEquip, ERPGItemBindState::AccountBound })
	{ auto R = MakeReward(); R.BindState = Bind; Reject(R); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonEquipmentRewardAuthoringTest,
	"ProjectRPG.Online.DungeonReward.EquipmentProgression.Authoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonEquipmentRewardAuthoringTest::RunTest(const FString&)
{
	using namespace RPGDungeonEquipmentRewardTests;
	auto* Definition = MakeDefinition();
	FString Version, Error; TArray<FRPGCurrencyChange> Currency; TArray<FRPGDungeonItemReward> Items;
	if (!TestTrue(TEXT("Old authored reward builds"), Definition->BuildSettlement(Version, Currency, Items, Error))) return false;
	if (!TestEqual(TEXT("One item"), Items.Num(), 1)) return false;
	TestFalse(TEXT("Existing authoring never silently grants +0"), Items[0].EquipmentProgression.IsSet());
	Definition->ItemRewards[0].bIncludeEquipmentProgression = true;
	for (int32 Level = 0; Level <= 3; ++Level)
	{
		Definition->ItemRewards[0].EquipmentProgressionLevel = Level;
		if (!TestTrue(TEXT("Explicit authored growth builds"), Definition->BuildSettlement(Version, Currency, Items, Error))) return false;
		TestTrue(TEXT("Builder supplies the full fixed band/rule/level"), Items.Num() == 1 && Items[0].EquipmentProgression == MakeReward(Names[3], Level).EquipmentProgression);
	}
	Definition->ItemRewards[0].EquipmentProgressionLevel = 4;
	TestFalse(TEXT("Native validation rejects out-of-editor range writes"), Definition->BuildSettlement(Version, Currency, Items, Error));
	TestTrue(TEXT("Failure clears prior outputs"), Version.IsEmpty() && Currency.IsEmpty() && Items.IsEmpty() && !Error.IsEmpty());
	Definition->ItemRewards[0].EquipmentProgressionLevel = 0;
	Definition->ItemRewards[0].BindState = ERPGItemBindState::Unbound;
	TestFalse(TEXT("Wrong binding fails the authored build too"), Definition->BuildSettlement(Version, Currency, Items, Error));
	Definition->ItemRewards[0].bIncludeEquipmentProgression = false;
	if (!TestTrue(TEXT("Disabling opt-in restores legacy authoring"), Definition->BuildSettlement(Version, Currency, Items, Error))) return false;
	TestFalse(TEXT("No stale growth leaks from the earlier build"), Items[0].EquipmentProgression.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonEquipmentRewardSnapshotTest,
	"ProjectRPG.Online.DungeonReward.EquipmentProgression.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonEquipmentRewardSnapshotTest::RunTest(const FString&)
{
	using namespace RPGDungeonEquipmentRewardTests;
	auto* Reward = MakeDefinition(); Reward->ItemRewards[0].bIncludeEquipmentProgression = true;
	auto* Definition = NewObject<URPGDungeonServerDefinition>();
	Definition->DungeonId = TEXT("Progression.RewardFixture");
	Definition->DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/testmap.testmap")));
	Definition->ClearRewardsByDifficulty.Add(ERPGGameDifficulty::Normal, Reward);
	TArray<TObjectPtr<URPGDungeonServerDefinition>> Definitions = { Definition };
	FRPGDungeonServerSelection Frozen, Next; FString Error;
	if (!TestTrue(TEXT("The actual assigned-content path builds growth"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, Definition->DungeonId, TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Frozen, Error))) return false;
	Reward->ItemRewards[0].EquipmentProgressionLevel = 3;
	if (!TestTrue(TEXT("A later session can use a new authored level"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, Definition->DungeonId, TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Next, Error))) return false;
	if (!TestTrue(TEXT("Both snapshots have one item"), Frozen.ItemRewards.Num() == 1 && Next.ItemRewards.Num() == 1)) return false;
	TestEqual(TEXT("The original immutable copy remains explicit +0"), Frozen.ItemRewards[0].EquipmentProgression.Level, 0);
	TestEqual(TEXT("Only the next snapshot gets +3"), Next.ItemRewards[0].EquipmentProgression.Level, 3);
	FJsonObject OriginalBody, NextBody;
	TestTrue(TEXT("Frozen original encodes"), Frozen.ItemRewards[0].WriteEquipmentProgression(OriginalBody));
	TestTrue(TEXT("Next encodes"), Next.ItemRewards[0].WriteEquipmentProgression(NextBody));
	TestEqual(TEXT("Wire original remains +0"), OriginalBody.GetObjectField(TEXT("equipmentProgression"))->GetIntegerField(TEXT("level")), 0);
	Reward->ItemRewards[0].EquipmentProgressionLevel = -1;
	TestFalse(TEXT("Invalid future authoring cannot become a new assignment"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, Definition->DungeonId, TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Next, Error));
	TestTrue(TEXT("Failed selection clears its candidate only"), Next.ItemRewards.IsEmpty() && Frozen.ItemRewards[0].EquipmentProgression.IsSet());
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonEquipmentRewardBlueprintSurfaceTest,
	"ProjectRPG.Online.DungeonReward.EquipmentProgression.BlueprintSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonEquipmentRewardBlueprintSurfaceTest::RunTest(const FString&)
{
	const auto* Include = FindFProperty<FBoolProperty>(FRPGDungeonItemRewardEntry::StaticStruct(), TEXT("bIncludeEquipmentProgression"));
	const auto* Level = FindFProperty<FIntProperty>(FRPGDungeonItemRewardEntry::StaticStruct(), TEXT("EquipmentProgressionLevel"));
	if (!TestTrue(TEXT("UHT exposes the actual authoring properties"), Include && Level)) return false;
	TestTrue(TEXT("Opt-in is editable and BP-readable"), Include->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly));
	TestTrue(TEXT("Level is editable and BP-readable"), Level->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly));
	TestEqual(TEXT("Level is conditional on explicit opt-in"), Level->GetMetaData(TEXT("EditCondition")), FString(TEXT("bIncludeEquipmentProgression")));
	TestEqual(TEXT("Editor minimum"), Level->GetMetaData(TEXT("ClampMin")), FString(TEXT("0")));
	TestEqual(TEXT("Editor maximum"), Level->GetMetaData(TEXT("ClampMax")), FString(TEXT("3")));
	for (const TCHAR* Field : { TEXT("GrowthBandId"), TEXT("RuleVersion"), TEXT("Level") })
	{
		const auto* Property = FindFProperty<FProperty>(FRPGEquipmentProgressionState::StaticStruct(), Field);
		if (!TestNotNull(TEXT("Persisted state field exists"), Property)) return false;
		TestFalse(TEXT("Persisted item state was not made editable for authoring"), Property->HasAnyPropertyFlags(CPF_Edit));
	}
	return true;
}
#endif
#endif
