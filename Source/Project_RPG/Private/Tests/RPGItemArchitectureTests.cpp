#if WITH_DEV_AUTOMATION_TESTS

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Policy/RPGItemStackPolicy.h"
#include "Item/RPGItemFactory.h"
#include "Item/RPGItemInstance.h"
#include "Misc/AutomationTest.h"
#include "RPGItemTags.h"

namespace RPGItemArchitectureTests
{
	URPGItemDefinition* MakeStackableDefinition()
	{
		URPGItemDefinition* Definition = NewObject<URPGItemDefinition>();
		Definition->ItemTag = RPGGameplayTags::GameItem_Craft_fruit;
		Definition->ItemCategory = EItemCategory::Craft;
		Definition->DisplayName = FText::FromString(TEXT("Test Item"));
		Definition->MaxStackSize = 10;

		URPGItemStatDefinitionFragment* StatFragment =
			NewObject<URPGItemStatDefinitionFragment>(Definition);
		FRPGItemStatRange& Range = StatFragment->StatRanges.AddDefaulted_GetRef();
		Range.StatTag = RPGGameplayTags::Fragment_StatMod_1;
		Range.Minimum = 5.0f;
		Range.Maximum = 5.0f;
		Definition->Fragments.Add(StatFragment);
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemDefinitionBuildStateTest,
	"ProjectRPG.Item.Architecture.DefinitionBuildsDeterministicState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGItemDefinitionBuildStateTest::RunTest(const FString& Parameters)
{
	URPGItemDefinition* Definition =
		RPGItemArchitectureTests::MakeStackableDefinition();
	const URPGItemStatDefinitionFragment* FoundFragment =
		Definition->FindFragmentByClass<URPGItemStatDefinitionFragment>();
	TestNotNull(TEXT("Typed fragment lookup finds the stat fragment"), FoundFragment);

	URPGItemInstance* First = URPGItemFactory::CreateItemInstanceWithSeed(
		GetTransientPackage(), Definition, 50, 1337);
	URPGItemInstance* Second = URPGItemFactory::CreateItemInstanceWithSeed(
		GetTransientPackage(), Definition, 3, 1337);
	TestNotNull(TEXT("Factory creates the first item"), First);
	TestNotNull(TEXT("Factory creates the second item"), Second);
	if (!First || !Second)
	{
		return false;
	}

	TestEqual(TEXT("Initial quantity is clamped by the definition"),
		First->GetQuantity(), 10);
	TestEqual(TEXT("A deterministic stat roll is stored"),
		First->GetState().GetStatValue(RPGGameplayTags::Fragment_StatMod_1),
		5.0f);
	TestTrue(TEXT("Identity is unique"),
		First->GetState().GetInstanceId() != Second->GetState().GetInstanceId());
	TestTrue(TEXT("Creation seed is retained"),
		First->GetState().GetGenerationSeed() == 1337);
	TestTrue(TEXT("Equivalent rolls remain stack-compatible"),
		FRPGItemStackPolicy::CanStack(*First, *Second));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemStackPolicyTransferTest,
	"ProjectRPG.Item.Architecture.StackPolicyTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGItemStackPolicyTransferTest::RunTest(const FString& Parameters)
{
	URPGItemDefinition* Definition =
		RPGItemArchitectureTests::MakeStackableDefinition();
	URPGItemInstance* Source = URPGItemFactory::CreateItemInstanceWithSeed(
		GetTransientPackage(), Definition, 8, 10);
	URPGItemInstance* Destination = URPGItemFactory::CreateItemInstanceWithSeed(
		GetTransientPackage(), Definition, 6, 20);
	TestNotNull(TEXT("Source item exists"), Source);
	TestNotNull(TEXT("Destination item exists"), Destination);
	if (!Source || !Destination)
	{
		return false;
	}

	const FRPGItemStackTransfer Transfer =
		FRPGItemStackPolicy::CalculateTransfer(*Source, *Destination, 7);
	TestEqual(TEXT("Transfer is capped by destination capacity"),
		Transfer.TransferredQuantity, 4);
	TestEqual(TEXT("Source remainder is calculated without mutation"),
		Transfer.SourceRemaining, 4);
	TestEqual(TEXT("Destination result reaches max stack"),
		Transfer.DestinationQuantity, 10);
	TestEqual(TEXT("Policy does not mutate the source"),
		Source->GetQuantity(), 8);
	TestEqual(TEXT("Policy does not mutate the destination"),
		Destination->GetQuantity(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemStackPolicyStateCompatibilityTest,
	"ProjectRPG.Item.Architecture.StackPolicyRejectsDifferentRolls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGItemStackPolicyStateCompatibilityTest::RunTest(
	const FString& Parameters)
{
	URPGItemDefinition* Definition =
		RPGItemArchitectureTests::MakeStackableDefinition();
	URPGItemInstance* First = URPGItemFactory::CreateItemInstanceWithSeed(
		GetTransientPackage(), Definition, 1, 1);

	URPGItemStatDefinitionFragment* StatFragment =
		const_cast<URPGItemStatDefinitionFragment*>(
			Definition->FindFragmentByClass<URPGItemStatDefinitionFragment>());
	StatFragment->StatRanges[0].Minimum = 9.0f;
	StatFragment->StatRanges[0].Maximum = 9.0f;
	URPGItemInstance* Second = URPGItemFactory::CreateItemInstanceWithSeed(
		GetTransientPackage(), Definition, 1, 2);

	TestNotNull(TEXT("First item exists"), First);
	TestNotNull(TEXT("Second item exists"), Second);
	if (!First || !Second)
	{
		return false;
	}

	TestFalse(TEXT("Different persistent rolls cannot be merged"),
		FRPGItemStackPolicy::CanStack(*First, *Second));
	return true;
}

#endif
