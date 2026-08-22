#if WITH_DEV_AUTOMATION_TESTS

#include "Economy/RPGDungeonRewardDefinition.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Misc/AutomationTest.h"
#include "RPGItemTags.h"

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
	}
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
	return true;
}

#endif
