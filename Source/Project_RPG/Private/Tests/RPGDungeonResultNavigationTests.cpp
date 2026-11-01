#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "UI/Lobby/RPGOnlineLobbyNavigation.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

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
		case EUINavigation::Left: Data = &From->Navigation->Left; break;
		case EUINavigation::Right: Data = &From->Navigation->Right; break;
		case EUINavigation::Next: Data = &From->Navigation->Next; break;
		case EUINavigation::Previous: Data = &From->Navigation->Previous; break;
		default: return false;
		}
		return Data->Rule == (Target ? EUINavigationRule::Explicit : EUINavigationRule::Stop)
			&& Data->Widget.Get() == Target;
	}
	void Enable(UWidget* Widget)
	{
		Widget->SetIsEnabled(true); Widget->SetVisibility(ESlateVisibility::Visible);
		for (auto* Parent = Widget->GetParent(); Parent; Parent = Parent->GetParent())
		{ Parent->SetIsEnabled(true); Parent->SetVisibility(ESlateVisibility::Visible); }
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonResultNavigationTest,
	"ProjectRPG.Online.DungeonResultNavigation.NativeControls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonResultNavigationTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Owned result-navigation world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGDungeonResultActionsWidget>(World, URPGDungeonResultActionsWidget::StaticClass());
	if (!TestNotNull(TEXT("Native result widget"), Widget)) return false;
	const auto Slate = Widget->TakeWidget();
	const auto Directions = {EUINavigation::Up, EUINavigation::Down, EUINavigation::Left,
		EUINavigation::Right, EUINavigation::Next, EUINavigation::Previous};
	if (!TestNotNull(TEXT("Native presentation already connects navigation"), Widget->ReturnButton->Navigation.Get())) return false;
	for (const auto Direction : Directions)
		TestTrue(TEXT("An unbound result has no navigable return action"), PointsTo(Widget->ReturnButton, Direction, nullptr));
	// UI-state fixtures only, not authenticated permission grants. No clicks,
	// requests, actual Slate-user focus or input-mode changes are dispatched.
	for (auto* Button : {Widget->RetryButton.Get(), Widget->ReturnButton.Get(), Widget->RefreshButton.Get(),
		Widget->DisconnectButton.Get(), Widget->AbandonButton.Get(), Widget->LoginSteamButton.Get(), Widget->LoginDevelopmentButton.Get()})
		if (Button) { Button->SetIsEnabled(false); Button->SetVisibility(ESlateVisibility::Collapsed); }
	Enable(Widget->ReturnButton); Enable(Widget->RefreshButton); Enable(Widget->LoginSteamButton);
	Widget->SetDesiredFocusWidget(Widget->ReturnButton);
	Widget->RefreshInputNavigation();
	TestTrue(TEXT("Horizontal next follows the enabled row order"), PointsTo(Widget->ReturnButton, EUINavigation::Right, Widget->RefreshButton));
	TestTrue(TEXT("Horizontal previous wraps within available result controls"), PointsTo(Widget->ReturnButton, EUINavigation::Left, Widget->LoginSteamButton));
	TestTrue(TEXT("Tab and down reuse the same available order"), PointsTo(Widget->RefreshButton, EUINavigation::Next, Widget->LoginSteamButton)
		&& PointsTo(Widget->RefreshButton, EUINavigation::Down, Widget->LoginSteamButton));
	TestTrue(TEXT("Reverse Tab and up wrap consistently"), PointsTo(Widget->ReturnButton, EUINavigation::Previous, Widget->LoginSteamButton)
		&& PointsTo(Widget->ReturnButton, EUINavigation::Up, Widget->LoginSteamButton));
	auto* Navigation = Widget->ReturnButton->Navigation.Get();
	Widget->RefreshInputNavigation();
	TestTrue(TEXT("Repeated configuration reuses navigation metadata"), Widget->ReturnButton->Navigation.Get() == Navigation);
	TestTrue(TEXT("Configuration does not select a new default focus target"), Widget->GetDesiredFocusWidget() == Widget->ReturnButton);
	Widget->RefreshButton->SetIsEnabled(false); Widget->RefreshInputNavigation();
	TestTrue(TEXT("Disabled action is skipped horizontally"), PointsTo(Widget->ReturnButton, EUINavigation::Right, Widget->LoginSteamButton));
	for (const auto Direction : Directions)
		TestTrue(TEXT("Disabled action loses stale links in every direction"), PointsTo(Widget->RefreshButton, Direction, nullptr));
	Widget->LoginSteamButton->SetVisibility(ESlateVisibility::Collapsed); Widget->RefreshInputNavigation();
	for (const auto Direction : Directions)
		TestTrue(TEXT("One available result action stays contained"), PointsTo(Widget->ReturnButton, Direction, nullptr));
	Enable(Widget->RefreshButton); Enable(Widget->LoginSteamButton); Widget->RefreshInputNavigation();
	Widget->ReturnButton->GetParent()->SetVisibility(ESlateVisibility::Collapsed); Widget->RefreshInputNavigation();
	TestTrue(TEXT("A hidden row makes its otherwise enabled actions unavailable"), PointsTo(Widget->ReturnButton, EUINavigation::Next, nullptr)
		&& PointsTo(Widget->RefreshButton, EUINavigation::Next, nullptr));
	for (const auto Direction : Directions)
		TestTrue(TEXT("The remaining visible row cannot escape"), PointsTo(Widget->LoginSteamButton, Direction, nullptr));
	Enable(Widget->ReturnButton); Enable(Widget->RefreshButton);
	Enable(Widget->ConfirmAbandonButton); Enable(Widget->CancelAbandonButton);
	Widget->bAbandonmentConfirmationOpen = true; Widget->RefreshInputNavigation();
	for (const auto Direction : Directions)
	{
		TestTrue(TEXT("Confirmation is contained in every direction"), PointsTo(Widget->ConfirmAbandonButton, Direction, Widget->CancelAbandonButton)
			&& PointsTo(Widget->CancelAbandonButton, Direction, Widget->ConfirmAbandonButton));
		TestTrue(TEXT("Background result actions cannot navigate behind the modal"), PointsTo(Widget->ReturnButton, Direction, nullptr));
	}
	Widget->ConfirmAbandonButton->SetIsEnabled(false); Widget->RefreshInputNavigation();
	for (const auto Direction : Directions)
		TestTrue(TEXT("A sole enabled cancel remains contained without selecting confirm"), PointsTo(Widget->CancelAbandonButton, Direction, nullptr));
	Widget->bAbandonmentConfirmationOpen = false;
	Widget->AbandonmentConfirmation->SetVisibility(ESlateVisibility::Collapsed); Widget->RefreshInputNavigation();
	TestTrue(TEXT("Closing the modal restores the ordinary row cycle"), PointsTo(Widget->ReturnButton, EUINavigation::Right, Widget->RefreshButton));
	Widget->InitializeResult(true);
	Widget->RefreshPresentation(); // InitializeResult selects mode; it does not refresh a live widget.
	TestFalse(TEXT("Navigation never grants return authority"), Widget->CurrentPresentation().bCanReturn);
	for (const auto Direction : Directions)
		TestTrue(TEXT("A subsequent real presentation clears fixture availability"), PointsTo(Widget->ReturnButton, Direction, nullptr));

	auto* Custom = CreateWidget<URPGDungeonResultActionsWidget>(World, URPGDungeonResultActionsWidget::StaticClass());
	if (!TestNotNull(TEXT("Authored-root fixture"), Custom)) return false;
	auto* Root = Custom->WidgetTree->ConstructWidget<UVerticalBox>(); Custom->WidgetTree->RootWidget = Root;
	auto* Button = Custom->WidgetTree->ConstructWidget<UButton>(); Root->AddChildToVerticalBox(Button);
	auto* Input = Custom->WidgetTree->ConstructWidget<UEditableTextBox>(); Root->AddChildToVerticalBox(Input);
	auto* Combo = Custom->WidgetTree->ConstructWidget<UComboBoxString>(); Root->AddChildToVerticalBox(Combo);
	Button->SetNavigationRuleBase(EUINavigation::Next, EUINavigationRule::Escape);
	Input->SetNavigationRuleBase(EUINavigation::Right, EUINavigationRule::Escape);
	Input->SetText(FText::FromString(TEXT("retained input"))); Combo->AddOption(TEXT("retained selection")); Combo->SetSelectedIndex(0);
	const auto CustomSlate = Custom->TakeWidget(); Custom->RefreshInputNavigation();
	TestEqual(TEXT("An authored root keeps its navigation"), Button->Navigation->Next.Rule, EUINavigationRule::Escape);
	RPGOnlineLobbyNavigation::ConfigureButtonRows(*Custom);
	TestEqual(TEXT("Even explicit button-row mode preserves text-caret navigation"), Input->Navigation->Right.Rule, EUINavigationRule::Escape);
	TestEqual(TEXT("Configuration preserves entered text"), Input->GetText().ToString(), FString(TEXT("retained input")));
	TestEqual(TEXT("Configuration preserves combo selection"), Combo->GetSelectedIndex(), 0);
	return true;
}
#endif
