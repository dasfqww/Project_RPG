#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyNavigation.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/VerticalBox.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"

namespace
{
	struct FFixture
	{
		TStrongObjectPtr<URPGDungeonResultActionsWidget> Screen{NewObject<URPGDungeonResultActionsWidget>()};
		TSharedPtr<SWidget> Slate;
		UVerticalBox* Root;
		UVerticalBox* Group;
		UButton* Action;
		UEditableTextBox* Input;
		UVerticalBox* Modal;
		UButton* Confirm;
		UButton* Cancel;
		FFixture()
		{
			Screen->Initialize();
			Root = Screen->WidgetTree->ConstructWidget<UVerticalBox>(); Screen->WidgetTree->RootWidget = Root;
			Group = Screen->WidgetTree->ConstructWidget<UVerticalBox>(); Root->AddChildToVerticalBox(Group);
			Action = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Action")); Group->AddChildToVerticalBox(Action);
			Input = Screen->WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Input")); Root->AddChildToVerticalBox(Input);
			Input->SetText(FText::FromString(TEXT("keep this text")));
			Modal = Screen->WidgetTree->ConstructWidget<UVerticalBox>(); Root->AddChildToVerticalBox(Modal);
			Confirm = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Confirm"));
			Cancel = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Cancel"));
			Modal->AddChildToVerticalBox(Confirm); Modal->AddChildToVerticalBox(Cancel); Modal->SetVisibility(ESlateVisibility::Collapsed);
			Slate = Screen->TakeWidget(); Screen->SetIsFocusable(true); Screen->SetDesiredFocusWidget(Input);
		}
		UWidget* Find(const TSharedPtr<SWidget>& Focused, UWidget* Scope = nullptr)
		{ return RPGOnlineLobbyNavigation::FindFocusRecoveryTarget(*Screen, Focused, Scope); }
	};
	TSharedPtr<SWidget> EditableLeaf(const TSharedPtr<SWidget>& Root)
	{
		if (!Root) return nullptr;
		if (Root->GetTypeAsString() == TEXT("SEditableText")) return Root;
		if (FChildren* Children = Root->GetChildren())
			for (int32 Index = 0; Index < Children->Num(); ++Index)
				if (const auto Found = EditableLeaf(Children->GetChildAt(Index))) return Found;
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyStaleChildFocusTest,
	"ProjectRPG.Online.DungeonLobbyFocusRecovery.StaleChildAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyStaleChildFocusTest::RunTest(const FString&)
{
	FFixture F;
	const auto Focused = F.Action->GetCachedWidget();
	TestNull(TEXT("Healthy owned focus is left alone"), F.Find(Focused));
	F.Action->SetIsEnabled(false);
	TestEqual(TEXT("A newly disabled action recovers to the existing safe preferred input"), F.Find(Focused), static_cast<UWidget*>(F.Input));
	F.Action->SetIsEnabled(true); F.Group->SetVisibility(ESlateVisibility::Collapsed);
	TestEqual(TEXT("A newly hidden ancestor is detected in the cached Slate tree"), F.Find(Focused), static_cast<UWidget*>(F.Input));
	F.Group->SetVisibility(ESlateVisibility::Visible); F.Group->SetIsEnabled(false);
	TestEqual(TEXT("A disabled ancestor cannot keep stale child focus"), F.Find(Focused), static_cast<UWidget*>(F.Input));
	F.Input->SetIsEnabled(false);
	TestEqual(TEXT("When every action is busy, the inert screen root is the fallback"), F.Find(Focused), static_cast<UWidget*>(F.Screen.Get()));
	F.Screen->SetDesiredFocusWidget(NAME_None);
	TestEqual(TEXT("Missing preferred focus never guesses an actionable/destructive default"), F.Find(Focused), static_cast<UWidget*>(F.Screen.Get()));
	F.Screen->SetIsEnabled(false);
	TestNull(TEXT("A disabled screen does not request focus back"), F.Find(Focused));
	F.Screen->SetIsEnabled(true); F.Screen->SetVisibility(ESlateVisibility::Collapsed);
	TestNull(TEXT("A hidden screen does not request focus back"), F.Find(Focused));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyHealthyFocusScopeTest,
	"ProjectRPG.Online.DungeonLobbyFocusRecovery.InputAndForeignScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyHealthyFocusScopeTest::RunTest(const FString&)
{
	FFixture F;
	const auto TextFocus = EditableLeaf(F.Input->GetCachedWidget());
	if (!TestTrue(TEXT("The fixture contains a real Slate editable-text descendant"), TextFocus.IsValid())) return false;
	TestNull(TEXT("Healthy internal editor focus is not stolen"), F.Find(TextFocus));
	F.Input->SetIsEnabled(false); F.Screen->SetDesiredFocusWidget(F.Action);
	TestEqual(TEXT("A disabled internal editor can use an existing safe preferred action"), F.Find(TextFocus), static_cast<UWidget*>(F.Action));
	TestEqual(TEXT("Recovery queries do not edit input content"), F.Input->GetText().ToString(), FString(TEXT("keep this text")));
	TestEqual(TEXT("Recovery queries do not rewrite desired focus"), F.Screen->GetDesiredFocusWidget(), static_cast<UWidget*>(F.Action));
	const TSharedPtr<SWidget> Foreign = SNew(SButton);
	TestNull(TEXT("Another screen/window's Slate focus is not eligible"), F.Find(Foreign));
	TestNull(TEXT("Missing focus is not permission to restore old UI focus"), F.Find(TSharedPtr<SWidget>()));
	TestNull(TEXT("Root focus belongs to the existing default policy, not child recovery"), F.Find(F.Screen->GetCachedWidget()));
	const auto Detached = F.Action->GetCachedWidget(); F.Action->RemoveFromParent();
	TestNull(TEXT("A detached control no longer proves current screen ownership"), F.Find(Detached));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyModalFocusPriorityTest,
	"ProjectRPG.Online.DungeonLobbyFocusRecovery.ModalPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyModalFocusPriorityTest::RunTest(const FString&)
{
	FFixture F;
	F.Modal->SetVisibility(ESlateVisibility::Visible); F.Screen->SetDesiredFocusWidget(F.Cancel);
	TestEqual(TEXT("An open confirmation restores only the existing cancel preference"),
		F.Find(F.Action->GetCachedWidget(), F.Modal), static_cast<UWidget*>(F.Cancel));
	TestNull(TEXT("Healthy cancel focus remains untouched"), F.Find(F.Cancel->GetCachedWidget(), F.Modal));
	F.Confirm->SetIsEnabled(false);
	TestEqual(TEXT("Disabled confirm focus is repaired to cancel, not another background action"),
		F.Find(F.Confirm->GetCachedWidget(), F.Modal), static_cast<UWidget*>(F.Cancel));
	F.Cancel->SetIsEnabled(false); F.Confirm->SetIsEnabled(true);
	TestEqual(TEXT("Loss of cancel never implicitly chooses destructive confirm"),
		F.Find(F.Cancel->GetCachedWidget(), F.Modal), static_cast<UWidget*>(F.Screen.Get()));
	auto* ForeignModal = NewObject<UVerticalBox>();
	TestNull(TEXT("An unrelated confirmation cannot authorize recovery"), F.Find(F.Action->GetCachedWidget(), ForeignModal));
	TestNull(TEXT("Without a modal, healthy background focus is valid again"), F.Find(F.Action->GetCachedWidget()));
	return true;
}
#endif
