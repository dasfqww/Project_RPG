#if WITH_DEV_AUTOMATION_TESTS
#include "UI/RPGMenuFocus.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FMenuFocusFixture
	{
		TStrongObjectPtr<URPGDungeonResultActionsWidget> Screen{NewObject<URPGDungeonResultActionsWidget>()};
		TStrongObjectPtr<URPGDungeonResultActionsWidget> Other{NewObject<URPGDungeonResultActionsWidget>()};
		TSharedPtr<SWidget> ScreenSlate;
		TSharedPtr<SWidget> OtherSlate;
		UVerticalBox* Panel;
		UButton* First;
		UEditableTextBox* Input;
		UTextBlock* Label;
		UButton* Foreign;
		FMenuFocusFixture()
		{
			Screen->Initialize(); Other->Initialize();
			Panel = Screen->WidgetTree->ConstructWidget<UVerticalBox>();
			Screen->WidgetTree->RootWidget = Panel;
			First = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("First"));
			Input = Screen->WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Input"));
			Label = Screen->WidgetTree->ConstructWidget<UTextBlock>();
			Panel->AddChildToVerticalBox(First); Panel->AddChildToVerticalBox(Input); Panel->AddChildToVerticalBox(Label);
			Foreign = Other->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Foreign"));
			Other->WidgetTree->RootWidget = Foreign;
			ScreenSlate = Screen->TakeWidget(); OtherSlate = Other->TakeWidget();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMenuFocusSelectionTest, "ProjectRPG.Online.MenuFocus.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMenuFocusSelectionTest::RunTest(const FString&)
{
	FMenuFocusFixture F;
	RPGMenuFocus::Update(*F.Screen, {nullptr, F.Foreign, F.Label, F.First, F.Input});
	TestEqual(TEXT("Only an eligible owned control can be the preferred focus"), F.Screen->GetDesiredFocusWidget(), static_cast<UWidget*>(F.First));
	F.First->SetIsEnabled(false);
	RPGMenuFocus::Update(*F.Screen, {F.First, F.Input});
	TestEqual(TEXT("Disabled actions are skipped for an input field"), F.Screen->GetDesiredFocusWidget(), static_cast<UWidget*>(F.Input));
	F.First->SetIsEnabled(true);
	for (const auto Visibility : {ESlateVisibility::Hidden, ESlateVisibility::Collapsed})
	{
		F.First->SetVisibility(Visibility);
		RPGMenuFocus::Update(*F.Screen, {F.First, F.Input});
		TestEqual(TEXT("Hidden controls are skipped"), F.Screen->GetDesiredFocusWidget(), static_cast<UWidget*>(F.Input));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMenuFocusAvailabilityTest, "ProjectRPG.Online.MenuFocus.Availability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMenuFocusAvailabilityTest::RunTest(const FString&)
{
	FMenuFocusFixture F;
	RPGMenuFocus::Update(*F.Screen, {F.First});
	F.First->SetIsEnabled(false);
	RPGMenuFocus::Update(*F.Screen, {F.First});
	TestTrue(TEXT("No eligible action clears stale desired focus"), F.Screen->GetDesiredFocusWidgetName().IsNone());
	F.First->SetIsEnabled(true);
	RPGMenuFocus::Update(*F.Screen, {F.First});
	TestEqual(TEXT("A late enabled action becomes eligible without triggering its click"), F.Screen->GetDesiredFocusWidget(), static_cast<UWidget*>(F.First));
	for (const auto Visibility : {ESlateVisibility::Hidden, ESlateVisibility::Collapsed})
	{
		F.Panel->SetVisibility(Visibility);
		RPGMenuFocus::Update(*F.Screen, {F.First});
		TestTrue(TEXT("Children of hidden confirmation/panels cannot receive desired focus"), F.Screen->GetDesiredFocusWidgetName().IsNone());
	}
	F.Panel->SetVisibility(ESlateVisibility::Visible); F.Panel->SetIsEnabled(false);
	RPGMenuFocus::Update(*F.Screen, {F.First});
	TestTrue(TEXT("Disabled ancestors cannot be bypassed"), F.Screen->GetDesiredFocusWidgetName().IsNone());
	return true;
}
#endif
