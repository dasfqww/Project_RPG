#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyNavigation.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	bool PointsTo(const UWidget* From, EUINavigation Direction, const UWidget* Target)
	{
		if (!From || !From->Navigation) return false;
		const FWidgetNavigationData* Data = nullptr;
		switch (Direction)
		{
		case EUINavigation::Up: Data = &From->Navigation->Up; break;
		case EUINavigation::Down: Data = &From->Navigation->Down; break;
		case EUINavigation::Next: Data = &From->Navigation->Next; break;
		case EUINavigation::Previous: Data = &From->Navigation->Previous; break;
		case EUINavigation::Left: Data = &From->Navigation->Left; break;
		case EUINavigation::Right: Data = &From->Navigation->Right; break;
		default: return false;
		}
		return Data->Rule == (Target ? EUINavigationRule::Explicit : EUINavigationRule::Stop)
			&& Data->Widget.Get() == Target;
	}
	struct FFixture
	{
		TStrongObjectPtr<URPGDungeonResultActionsWidget> Screen{NewObject<URPGDungeonResultActionsWidget>()};
		TSharedPtr<SWidget> Slate;
		UVerticalBox* Root;
		UButton* First;
		UButton* Hidden;
		UVerticalBox* HiddenSection;
		UButton* Disabled;
		UEditableTextBox* Input;
		UComboBoxString* Combo;
		URPGDungeonResultActionsWidget* Nested;
		UButton* NestedButton;
		UButton* Last;
		UVerticalBox* Modal;
		UButton* Confirm;
		UButton* Cancel;
		FFixture()
		{
			Screen->Initialize();
			Root = Screen->WidgetTree->ConstructWidget<UVerticalBox>();
			Screen->WidgetTree->RootWidget = Root;
			First = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("First"));
			Root->AddChildToVerticalBox(First);
			HiddenSection = Screen->WidgetTree->ConstructWidget<UVerticalBox>();
			Root->AddChildToVerticalBox(HiddenSection);
			Hidden = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Hidden"));
			HiddenSection->AddChildToVerticalBox(Hidden);
			HiddenSection->SetVisibility(ESlateVisibility::Collapsed);
			Disabled = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Disabled"));
			Root->AddChildToVerticalBox(Disabled); Disabled->SetIsEnabled(false);
			Root->AddChildToVerticalBox(Screen->WidgetTree->ConstructWidget<UTextBlock>());
			Input = Screen->WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Input"));
			Root->AddChildToVerticalBox(Input); Input->SetText(FText::FromString(TEXT("unchanged")));
			Combo = Screen->WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Combo"));
			Root->AddChildToVerticalBox(Combo); Combo->AddOption(TEXT("A")); Combo->AddOption(TEXT("B"));
			Combo->SetSelectedOption(TEXT("B")); Combo->SetEnableGamepadNavigationMode(false);
			Nested = Screen->WidgetTree->ConstructWidget<URPGDungeonResultActionsWidget>();
			Nested->Initialize();
			auto* NestedRoot = Nested->WidgetTree->ConstructWidget<UVerticalBox>();
			Nested->WidgetTree->RootWidget = NestedRoot;
			NestedButton = Nested->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("NestedAction"));
			NestedRoot->AddChildToVerticalBox(NestedButton); Root->AddChildToVerticalBox(Nested);
			Last = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Last"));
			Root->AddChildToVerticalBox(Last);
			Modal = Screen->WidgetTree->ConstructWidget<UVerticalBox>(); Root->AddChildToVerticalBox(Modal);
			Confirm = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Confirm"));
			Cancel = Screen->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Cancel"));
			Modal->AddChildToVerticalBox(Confirm); Modal->AddChildToVerticalBox(Cancel);
			Modal->SetVisibility(ESlateVisibility::Collapsed);
			Slate = Screen->TakeWidget();
			Input->SetNavigationRuleBase(EUINavigation::Left, EUINavigationRule::Stop);
			Input->SetNavigationRuleExplicit(EUINavigation::Right, Last);
			Hidden->SetNavigationRuleExplicit(EUINavigation::Next, Last);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyNavigationAvailabilityTest,
	"ProjectRPG.Online.DungeonLobbyNavigation.OrderAndAvailability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyNavigationAvailabilityTest::RunTest(const FString&)
{
	FFixture F;
	F.Screen->SetDesiredFocusWidget(F.Input);
	RPGOnlineLobbyNavigation::Configure(*F.Screen);
	const TArray<UWidget*> Expected = {F.First, F.Input, F.Combo, F.NestedButton, F.Last};
	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		UWidget* Previous = Expected[(Index + Expected.Num() - 1) % Expected.Num()];
		UWidget* Next = Expected[(Index + 1) % Expected.Num()];
		TestTrue(TEXT("Tab/down follow visual order across nested WidgetTrees"),
			PointsTo(Expected[Index], EUINavigation::Next, Next) && PointsTo(Expected[Index], EUINavigation::Down, Next));
		TestTrue(TEXT("Shift-Tab/up wrap to the preceding available control"),
			PointsTo(Expected[Index], EUINavigation::Previous, Previous) && PointsTo(Expected[Index], EUINavigation::Up, Previous));
	}
	TestTrue(TEXT("A hidden ancestor clears its child's old navigation"), PointsTo(F.Hidden, EUINavigation::Next, nullptr));
	TestTrue(TEXT("Disabled actions never enter the navigation cycle"), PointsTo(F.Disabled, EUINavigation::Down, nullptr));
	TestTrue(TEXT("Closed gamepad combos navigate until explicitly accepted"), F.Combo->IsEnableGamepadNavigationMode());
	TestEqual(TEXT("Configuring navigation does not select a combo option"), F.Combo->GetSelectedOption(), FString(TEXT("B")));
	TestEqual(TEXT("Configuring navigation does not edit an input"), F.Input->GetText().ToString(), FString(TEXT("unchanged")));
	TestTrue(TEXT("Input caret/spatial left-right rules are retained"), PointsTo(F.Input, EUINavigation::Left, nullptr)
		&& PointsTo(F.Input, EUINavigation::Right, F.Last));
	TestEqual(TEXT("Navigation configuration does not replace desired focus"), F.Screen->GetDesiredFocusWidget(), static_cast<UWidget*>(F.Input));
	RPGOnlineLobbyNavigation::Configure(*F.Screen);
	TestTrue(TEXT("Unchanged refresh preserves the same navigation"), PointsTo(F.First, EUINavigation::Next, F.Input));
	F.Input->SetIsEnabled(false); F.Combo->SetVisibility(ESlateVisibility::Collapsed);
	F.Nested->SetIsEnabled(false); F.HiddenSection->SetVisibility(ESlateVisibility::Visible);
	RPGOnlineLobbyNavigation::Configure(*F.Screen);
	TestTrue(TEXT("Availability changes rebuild the cycle without stale targets"), PointsTo(F.First, EUINavigation::Next, F.Hidden)
		&& PointsTo(F.Hidden, EUINavigation::Next, F.Last) && PointsTo(F.Last, EUINavigation::Next, F.First));
	TestTrue(TEXT("A disabled nested UserWidget gates controls in its separate tree"), PointsTo(F.NestedButton, EUINavigation::Next, nullptr));
	F.First->SetVisibility(ESlateVisibility::Collapsed); F.HiddenSection->SetVisibility(ESlateVisibility::Collapsed);
	RPGOnlineLobbyNavigation::Configure(*F.Screen);
	TestTrue(TEXT("A single enabled action stops instead of escaping or targeting itself"),
		PointsTo(F.Last, EUINavigation::Next, nullptr) && PointsTo(F.Last, EUINavigation::Up, nullptr));
	F.Screen->SetIsEnabled(false);
	RPGOnlineLobbyNavigation::Configure(*F.Screen);
	TestTrue(TEXT("A disabled screen cannot retain an actionable navigation target"), PointsTo(F.Last, EUINavigation::Down, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyNavigationModalTest,
	"ProjectRPG.Online.DungeonLobbyNavigation.ModalIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyNavigationModalTest::RunTest(const FString&)
{
	FFixture F;
	F.Modal->SetVisibility(ESlateVisibility::Visible);
	RPGOnlineLobbyNavigation::Configure(*F.Screen, F.Modal);
	for (const auto Direction : {EUINavigation::Up, EUINavigation::Down, EUINavigation::Next,
		EUINavigation::Previous, EUINavigation::Left, EUINavigation::Right})
	{
		TestTrue(TEXT("All confirmation directions remain between confirm/cancel"), PointsTo(F.Confirm, Direction, F.Cancel)
			&& PointsTo(F.Cancel, Direction, F.Confirm));
	}
	TestTrue(TEXT("A still-enabled background action cannot be reached through the modal graph"), PointsTo(F.First, EUINavigation::Next, nullptr));
	F.Confirm->SetIsEnabled(false);
	RPGOnlineLobbyNavigation::Configure(*F.Screen, F.Modal);
	TestTrue(TEXT("Disabled confirmation leaves only cancel, without a background escape"),
		PointsTo(F.Cancel, EUINavigation::Next, nullptr) && PointsTo(F.Cancel, EUINavigation::Left, nullptr)
		&& PointsTo(F.Confirm, EUINavigation::Down, nullptr));
	F.Modal->SetVisibility(ESlateVisibility::Collapsed);
	RPGOnlineLobbyNavigation::Configure(*F.Screen, F.Modal);
	TestTrue(TEXT("A hidden modal is not authority to enable background navigation"), PointsTo(F.Last, EUINavigation::Up, nullptr));
	RPGOnlineLobbyNavigation::Configure(*F.Screen);
	TestTrue(TEXT("Closing the modal restores the normal available cycle"), PointsTo(F.First, EUINavigation::Next, F.Input));
	F.Confirm->SetNavigationRuleExplicit(EUINavigation::Next, F.Cancel);
	RPGOnlineLobbyNavigation::Configure(*F.Screen, F.Modal, false);
	TestTrue(TEXT("A known closed confirmation keeps its policy outside the normal cycle"), PointsTo(F.Confirm, EUINavigation::Next, F.Cancel)
		&& PointsTo(F.First, EUINavigation::Next, F.Input));
	auto* Foreign = NewObject<UVerticalBox>();
	RPGOnlineLobbyNavigation::Configure(*F.Screen, Foreign);
	TestTrue(TEXT("An unrelated modal does not mutate this screen"), PointsTo(F.First, EUINavigation::Next, F.Input));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyNavigationNativeTest,
	"ProjectRPG.Online.DungeonLobbyNavigation.NativeConnection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyNavigationNativeTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Native lobby navigation world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGOnlineLobbyWidget>(World, URPGOnlineLobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("Native lobby"), Widget)) return false;
	const auto Slate = Widget->TakeWidget();
	auto Enable = [](UWidget* Control)
	{
		if (!Control) return;
		Control->SetIsEnabled(true); Control->SetVisibility(ESlateVisibility::Visible);
		for (auto* Parent = Control->GetParent(); Parent; Parent = Parent->GetParent())
		{ Parent->SetIsEnabled(true); Parent->SetVisibility(ESlateVisibility::Visible); }
	};
	// Navigation-only tree states. No account/session injection, click dispatch,
	// HTTP or gameplay opt-out is performed by this fixture.
	auto* Login = Widget->GetWidgetFromName(TEXT("LoginSteamButton"));
	auto* Logout = Widget->GetWidgetFromName(TEXT("LogoutBackendButton"));
	auto* Characters = Widget->GetWidgetFromName(TEXT("OwnedCharacters"));
	auto* Parties = Widget->GetWidgetFromName(TEXT("FindSteamPartiesButton"));
	if (!TestNotNull(TEXT("Login control"), Login) || !TestNotNull(TEXT("Account control"), Logout)
		|| !TestNotNull(TEXT("Character control"), Characters) || !TestNotNull(TEXT("Steam party control"), Parties)) return false;
	for (auto* Control : {Login, Logout, Characters, Parties}) Enable(Control);
	Widget->RefreshInputNavigation();
	TestTrue(TEXT("Native refresh connects the login/account controls"), PointsTo(Login, EUINavigation::Next, Logout));
	TestTrue(TEXT("Native refresh crosses into character selection"), PointsTo(Logout, EUINavigation::Next, Characters));
	TestTrue(TEXT("Steam party navigation is not omitted from the native cycle"), PointsTo(Characters, EUINavigation::Next, Parties)
		&& PointsTo(Parties, EUINavigation::Next, Login));
	auto* Confirm = Widget->GetWidgetFromName(TEXT("ConfirmDungeonAbandonmentButton"));
	auto* Cancel = Widget->GetWidgetFromName(TEXT("CancelDungeonAbandonmentButton"));
	if (!TestNotNull(TEXT("Native confirm"), Confirm) || !TestNotNull(TEXT("Native cancel"), Cancel)) return false;
	Enable(Confirm); Enable(Cancel); Widget->bAbandonmentConfirmationOpen = true;
	const bool bExistingFrontendBusy = Widget->IsFrontendRequestInFlight();
	Widget->RefreshInputNavigation();
	TestTrue(TEXT("Native confirmation scope replaces the background cycle"), PointsTo(Confirm, EUINavigation::Next, Cancel)
		&& PointsTo(Cancel, EUINavigation::Next, Confirm) && PointsTo(Parties, EUINavigation::Next, nullptr));
	TestEqual(TEXT("Navigation retains the existing frontend/modal busy state"), Widget->IsFrontendRequestInFlight(), bExistingFrontendBusy);
	TStrongObjectPtr<URPGOnlineLobbyWidget> Custom{NewObject<URPGOnlineLobbyWidget>()};
	Custom->Initialize();
	auto* CustomRoot = Custom->WidgetTree->ConstructWidget<UButton>(); Custom->WidgetTree->RootWidget = CustomRoot;
	CustomRoot->SetNavigationRuleBase(EUINavigation::Next, EUINavigationRule::Escape);
	Custom->RefreshInputNavigation();
	TestEqual(TEXT("An authored non-native root retains its own navigation"), CustomRoot->Navigation->Next.Rule, EUINavigationRule::Escape);
	return true;
}
#endif
