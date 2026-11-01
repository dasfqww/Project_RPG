#if WITH_DEV_AUTOMATION_TESTS

#include "Economy/RPGDungeonRewardDefinition.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Misc/AutomationTest.h"
#include "RPGItemTags.h"
#include "Online/RPGDungeonServerDefinition.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "GameMode/RPGGameModeBase.h"
#include "Misc/DataValidation.h"
#include "UObject/UnrealType.h"
#endif

namespace RPGDungeonRewardDefinitionTests
{
	URPGItemDefinition* MakeItemDefinition()
	{
		URPGItemDefinition* Definition = NewObject<URPGItemDefinition>();
		Definition->ItemTag = RPGGameplayTags::GameItem_Craft_fruit;
		Definition->DefinitionVersion = 3;
		Definition->MaxStackSize = 10;
		return Definition;
	}

	URPGDungeonRewardDefinition* MakeRewardDefinition(
		URPGItemDefinition* ItemDefinition)
	{
		URPGDungeonRewardDefinition* Reward =
			NewObject<URPGDungeonRewardDefinition>();
		Reward->RewardVersion = TEXT("boss_easy_v1");

		FRPGCurrencyChange& Currency =
			Reward->CurrencyChanges.AddDefaulted_GetRef();
		Currency.CurrencyCode = TEXT("Gold");
		Currency.Delta = 100;

		FRPGDungeonItemRewardEntry& Item =
			Reward->ItemRewards.AddDefaulted_GetRef();
		Item.ItemDefinition = ItemDefinition;
		Item.Quantity = 2;
		return Reward;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGDungeonRewardDefinitionBuildTest,
	"ProjectRPG.Online.DungeonReward.DefinitionBuildsPersistentPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonRewardDefinitionBuildTest::RunTest(const FString& Parameters)
{
	URPGItemDefinition* ItemDefinition =
		RPGDungeonRewardDefinitionTests::MakeItemDefinition();
	URPGDungeonRewardDefinition* Definition =
		RPGDungeonRewardDefinitionTests::MakeRewardDefinition(ItemDefinition);

	FString RewardVersion;
	TArray<FRPGCurrencyChange> CurrencyChanges;
	TArray<FRPGDungeonItemReward> ItemRewards;
	FString Error;
	TestTrue(TEXT("A valid authored reward builds"),
		Definition->BuildSettlement(
			RewardVersion,
			CurrencyChanges,
			ItemRewards,
			Error));
	TestEqual(TEXT("Reward version is retained"),
		RewardVersion,
		FString(TEXT("boss_easy_v1")));
	TestEqual(TEXT("Currency reward is retained"), CurrencyChanges.Num(), 1);
	TestEqual(TEXT("Item reward is built"), ItemRewards.Num(), 1);
	if (ItemRewards.Num() == 1)
	{
		TestEqual(TEXT("Definition type comes from the primary asset"),
			ItemRewards[0].DefinitionType,
			FName(TEXT("RPGItemDefinition")));
		TestEqual(TEXT("Definition name comes from the item tag"),
			ItemRewards[0].DefinitionName,
			ItemDefinition->ItemTag.GetTagName());
		TestEqual(TEXT("Definition version comes from the item asset"),
			ItemRewards[0].DefinitionVersion,
			3);
		TestEqual(TEXT("Inventory delivery stack limit comes from the item asset"),
			ItemRewards[0].MaxStackSize,
			10);
	}
#if WITH_EDITOR
	FDataValidationContext Validation;
	TestEqual(TEXT("A successfully checked ordinary reward is explicitly Valid"),
		Definition->IsDataValid(Validation), EDataValidationResult::Valid);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGDungeonRewardDefinitionRejectsInvalidContentTest,
	"ProjectRPG.Online.DungeonReward.DefinitionRejectsInvalidContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonRewardDefinitionRejectsInvalidContentTest::RunTest(
	const FString& Parameters)
{
	URPGItemDefinition* ItemDefinition =
		RPGDungeonRewardDefinitionTests::MakeItemDefinition();
	URPGDungeonRewardDefinition* Definition =
		RPGDungeonRewardDefinitionTests::MakeRewardDefinition(ItemDefinition);
	Definition->ItemRewards[0].Quantity = 11;

	FString RewardVersion;
	TArray<FRPGCurrencyChange> CurrencyChanges;
	TArray<FRPGDungeonItemReward> ItemRewards;
	FString Error;
	TestFalse(TEXT("An item reward cannot exceed its stack limit"),
		Definition->BuildSettlement(
			RewardVersion,
			CurrencyChanges,
			ItemRewards,
			Error));
	TestTrue(TEXT("A validation error is returned"), !Error.IsEmpty());
	TestTrue(TEXT("Failed builds leave no partial payload"),
		RewardVersion.IsEmpty()
			&& CurrencyChanges.IsEmpty()
			&& ItemRewards.IsEmpty());

	Definition->ItemRewards[0].Quantity = 1;
	ItemDefinition->DefinitionVersion = 0;
	TestFalse(TEXT("An invalid raw definition version is rejected"),
		Definition->BuildSettlement(
			RewardVersion,
			CurrencyChanges,
			ItemRewards,
			Error));

	ItemDefinition->DefinitionVersion = 3;
	ItemDefinition->MaxStackSize = 0;
	TestFalse(TEXT("An invalid raw stack limit is rejected"),
		Definition->BuildSettlement(
			RewardVersion,
			CurrencyChanges,
			ItemRewards,
			Error));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGProgressionOnlyRewardDefinitionTest,
	"ProjectRPG.Online.DungeonReward.ExplicitProgressionOnlyPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGProgressionOnlyRewardDefinitionTest::RunTest(const FString&)
{
	auto* Reward = NewObject<URPGDungeonRewardDefinition>();
	Reward->RewardVersion = TEXT("FirstSlice.v1");
	FString Version = TEXT("old-version"), Error = TEXT("old-error");
	TArray<FRPGCurrencyChange> Currency;
	TArray<FRPGDungeonItemReward> Items;
	Currency.AddDefaulted(); Items.AddDefaulted();
	TestTrue(TEXT("The authored progression version explicitly permits XP-only completion"),
		Reward->BuildSettlement(Version, Currency, Items, Error));
	TestEqual(TEXT("Progression version reaches settlement unchanged"), Version, FString(TEXT("FirstSlice.v1")));
	TestTrue(TEXT("No fake gold or item is introduced"), Currency.IsEmpty() && Items.IsEmpty() && Error.IsEmpty());
	TestTrue(TEXT("Building a transport payload leaves the authored bundle empty"), Reward->CurrencyChanges.IsEmpty() && Reward->ItemRewards.IsEmpty());
	FDataValidationContext Validation;
	TestEqual(TEXT("XP-only content is explicitly Valid after the native checks run"),
		Reward->IsDataValid(Validation), EDataValidationResult::Valid);
	for (const FName Invalid : { FName(NAME_None), FName(TEXT("pve_training_v1")), FName(TEXT("FirstSlice.v2")),
		FName(TEXT("no_reward")), FName(TEXT("firstslice.v1")) })
	{
		Reward->RewardVersion = Invalid;
		Version = TEXT("old-version"); Currency.AddDefaulted(); Items.AddDefaulted();
		TestFalse(TEXT("Other empty rewards do not silently become progression/no-reward clears"),
			Reward->BuildSettlement(Version, Currency, Items, Error));
		TestTrue(TEXT("Rejection clears stale payloads and explains the content error"),
			Version.IsEmpty() && Currency.IsEmpty() && Items.IsEmpty() && !Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGProgressionOnlyServerSelectionTest,
	"ProjectRPG.Online.DungeonReward.ProgressionOnlyAssignedSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGProgressionOnlyServerSelectionTest::RunTest(const FString&)
{
	auto* Reward = NewObject<URPGDungeonRewardDefinition>();
	Reward->RewardVersion = TEXT("FirstSlice.v1");
	auto* Definition = NewObject<URPGDungeonServerDefinition>();
	Definition->DungeonId = TEXT("Progression.Training");
	Definition->DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/testmap.testmap")));
	Definition->ClearRewardsByDifficulty.Add(ERPGGameDifficulty::Normal, Reward);
	TArray<TObjectPtr<URPGDungeonServerDefinition>> Definitions = { Definition };
	FRPGDungeonServerSelection Selection;
	FString Error;
	TestTrue(TEXT("XP-only rewards can pass server startup validation"), Definition->ValidateDefinition(Error));
	if (!TestTrue(TEXT("Assigned training/Normal builds through the existing server selection"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, TEXT("Progression.Training"),
			TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Selection, Error))) return false;
	TestEqual(TEXT("Startup snapshot retains the exact progression version"), Selection.RewardVersion, FString(TEXT("FirstSlice.v1")));
	TestTrue(TEXT("Startup snapshot has no currency/item transport entries"), Selection.CurrencyChanges.IsEmpty() && Selection.ItemRewards.IsEmpty());
	Reward->RewardVersion = TEXT("another_version");
	TestEqual(TEXT("A confirmed transport copy is not recalculated by later author edits"), Selection.RewardVersion, FString(TEXT("FirstSlice.v1")));
	TestFalse(TEXT("A later invalid empty definition cannot become a new assignment"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, TEXT("Progression.Training"),
			TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Normal, Selection, Error));
	TestTrue(TEXT("Failed startup clears the previous candidate payload"), Selection.RewardVersion.IsEmpty());
	Reward->RewardVersion = TEXT("FirstSlice.v1");
	TestFalse(TEXT("Progression-only support does not relax the authored map"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, TEXT("Progression.Training"),
			TEXT("/Game/Maps/bossmap"), ERPGGameDifficulty::Normal, Selection, Error));
	TestFalse(TEXT("Progression-only support does not invent an unsupported difficulty"),
		URPGDungeonServerDefinition::BuildAssignedSelection(Definitions, TEXT("Progression.Training"),
			TEXT("/Game/Maps/testmap"), ERPGGameDifficulty::Hard, Selection, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGDungeonRewardConfiguredAssetsTest,
	"ProjectRPG.Online.DungeonReward.ConfiguredAssetsAreValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonRewardConfiguredAssetsTest::RunTest(
	const FString& Parameters)
{
	struct FConfiguredRewardExpectation
	{
		const TCHAR* AssetPath;
		const TCHAR* RewardVersion;
		int64 Gold;
		int32 PotionQuantity;
		bool bIncludesHelm;
	};
	const FConfiguredRewardExpectation RewardExpectations[] =
	{
		{ TEXT("/Game/Blueprints/GameData/DungeonReward/DA_DungeonReward_Easy.DA_DungeonReward_Easy"), TEXT("pve_easy_v1"), 100, 1, false },
		{ TEXT("/Game/Blueprints/GameData/DungeonReward/DA_DungeonReward_Normal.DA_DungeonReward_Normal"), TEXT("pve_normal_v1"), 250, 2, false },
		{ TEXT("/Game/Blueprints/GameData/DungeonReward/DA_DungeonReward_Hard.DA_DungeonReward_Hard"), TEXT("pve_hard_v1"), 500, 3, true },
		{ TEXT("/Game/Blueprints/GameData/DungeonReward/DA_DungeonReward_Hell.DA_DungeonReward_Hell"), TEXT("pve_hell_v1"), 1000, 5, true }
	};
	for (const FConfiguredRewardExpectation& Expectation : RewardExpectations)
	{
		const URPGDungeonRewardDefinition* RewardDefinition =
			LoadObject<URPGDungeonRewardDefinition>(nullptr, Expectation.AssetPath);
		TestNotNull(Expectation.AssetPath, RewardDefinition);
		if (!RewardDefinition)
		{
			continue;
		}

		FString RewardVersion;
		TArray<FRPGCurrencyChange> CurrencyChanges;
		TArray<FRPGDungeonItemReward> ItemRewards;
		FString Error;
		TestTrue(FString::Printf(TEXT("%s builds a settlement payload"), Expectation.AssetPath),
			RewardDefinition->BuildSettlement(
				RewardVersion,
				CurrencyChanges,
				ItemRewards,
				Error));
		TestEqual(TEXT("Configured reward version matches the backend contract"),
			RewardVersion,
			FString(Expectation.RewardVersion));
		TestEqual(TEXT("Configured reward has one currency change"),
			CurrencyChanges.Num(),
			1);
		if (CurrencyChanges.Num() == 1)
		{
			TestEqual(TEXT("Configured currency is RosterGold"),
				CurrencyChanges[0].CurrencyCode,
				FName(TEXT("RosterGold")));
			TestEqual(TEXT("Configured RosterGold amount matches the backend contract"),
				CurrencyChanges[0].Delta,
				Expectation.Gold);
		}

		const int32 ExpectedItemCount = Expectation.bIncludesHelm ? 2 : 1;
		TestEqual(TEXT("Configured reward has the expected item count"),
			ItemRewards.Num(),
			ExpectedItemCount);
		if (ItemRewards.Num() >= 1)
		{
			TestEqual(TEXT("Configured potion uses the persistent item type"),
				ItemRewards[0].DefinitionType,
				FName(TEXT("RPGItemDefinition")));
			TestEqual(TEXT("Configured potion uses the stable item tag"),
				ItemRewards[0].DefinitionName,
				FName(TEXT("GameItem.Consume.Potion.Red.Large")));
			TestEqual(TEXT("Configured potion quantity matches the backend contract"),
				ItemRewards[0].Quantity,
				Expectation.PotionQuantity);
		}
		if (Expectation.bIncludesHelm && ItemRewards.Num() >= 2)
		{
			TestEqual(TEXT("Configured helm uses the persistent item type"),
				ItemRewards[1].DefinitionType,
				FName(TEXT("RPGItemDefinition")));
			TestEqual(TEXT("Configured helm uses the stable item tag"),
				ItemRewards[1].DefinitionName,
				FName(TEXT("GameItem.Equipment.Helm.Default")));
			TestEqual(TEXT("Configured helm quantity matches the backend contract"),
				ItemRewards[1].Quantity,
				1);
		}
	}

	const TCHAR* GameModePaths[] =
	{
		TEXT("/Game/Blueprints/GameMode/BP_SurvivalGameMode.BP_SurvivalGameMode"),
		TEXT("/Game/Blueprints/GameMode/BP_BossBattleGameMode.BP_BossBattleGameMode")
	};
	for (const TCHAR* GameModePath : GameModePaths)
	{
		const UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, GameModePath);
		TestNotNull(GameModePath, Blueprint);
		const ARPGGameModeBase* DefaultGameMode = Blueprint && Blueprint->GeneratedClass
			? Cast<ARPGGameModeBase>(Blueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		TestNotNull(TEXT("Content GameMode has an RPG GameMode default object"),
			DefaultGameMode);
		if (!DefaultGameMode)
		{
			continue;
		}

		const FBoolProperty* GiveRewardProperty = FindFProperty<FBoolProperty>(
			ARPGGameModeBase::StaticClass(),
			TEXT("bGiveReward"));
		TestNotNull(TEXT("bGiveReward property is available"), GiveRewardProperty);
		if (GiveRewardProperty)
		{
			TestTrue(TEXT("Content GameMode has backend rewards enabled"),
				GiveRewardProperty->GetPropertyValue_InContainer(DefaultGameMode));
		}

		FDataValidationContext ValidationContext;
		TestEqual(TEXT("Content GameMode reward configuration is valid"),
			DefaultGameMode->IsDataValid(ValidationContext),
			EDataValidationResult::Valid);
	}
	return true;
}
#endif

#endif
