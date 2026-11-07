#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/UniformGridPanel.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

struct FRPGInventoryTabsTestAccess
{
	static URPGInventoryProjectionWidget* MakeWidget()
	{
		auto* Widget = NewObject<URPGInventoryProjectionWidget>();
		Widget->WidgetTree = NewObject<UWidgetTree>(Widget);
		auto* Root = Widget->WidgetTree->ConstructWidget<UVerticalBox>();
		Widget->WidgetTree->RootWidget = Root;
		Widget->LoadStateText = Widget->WidgetTree->ConstructWidget<UTextBlock>();
		Widget->CommandResultText = Widget->WidgetTree->ConstructWidget<UTextBlock>();
		Widget->InventoryCapacityText = Widget->WidgetTree->ConstructWidget<UTextBlock>();
		Widget->InventoryGrid = Widget->WidgetTree->ConstructWidget<UUniformGridPanel>();
		Root->AddChild(Widget->LoadStateText); Root->AddChild(Widget->CommandResultText);
		Root->AddChild(Widget->InventoryCapacityText); Root->AddChild(Widget->InventoryGrid);
		Widget->InventoryViewModel = NewObject<URPGInventoryProjectionViewModel>(Widget);
		Widget->InventoryViewModel->LoadState = ERPGInventoryProjectionLoadState::Ready;
		Widget->EnsureBagTabControls();
		Widget->RefreshFromViewModel();
		return Widget;
	}
	static int32 Cells(URPGInventoryProjectionWidget& W) { return W.InventoryGrid->GetChildrenCount(); }
	static void ClickConsume(URPGInventoryProjectionWidget& W) { W.BagConsumablesButton->OnClicked.Broadcast(); }
	static void ClickOther(URPGInventoryProjectionWidget& W) { W.BagOtherButton->OnClicked.Broadcast(); }
	static void Pending(URPGInventoryProjectionViewModel& M, FGuid Request, FGuid Item)
	{ M.PendingInventorySorts.Add(Request).Revisions.Add(Item, 1); M.RefreshPendingItemCommandCount(); }
	static void Ack(URPGInventoryProjectionViewModel& M, FGuid Request, ERPGItemCommandResultCode Result)
	{ FRPGItemCommandClientResult Ack; Ack.RequestId = Request; Ack.Operation = TEXT("Item.Sort"); Ack.Result = Result; M.HandleCommandCompleted(Ack); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryTabsWidgetTest, "ProjectRPG.Item.InventoryTabs.Widget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryTabsWidgetTest::RunTest(const FString&)
{
	auto* Widget = FRPGInventoryTabsTestAccess::MakeWidget();
	TestEqual(TEXT("Default equipment tab"), Widget->GetSelectedInventoryCategory(), EItemCategory::Equip);
	TestEqual(TEXT("Equipment 80 cells"), FRPGInventoryTabsTestAccess::Cells(*Widget), 80);
	FRPGInventoryTabsTestAccess::ClickConsume(*Widget);
	TestEqual(TEXT("Actual button selects consumables"), Widget->GetSelectedInventoryCategory(), EItemCategory::Consume);
	TestEqual(TEXT("Consumables 80 cells"), FRPGInventoryTabsTestAccess::Cells(*Widget), 80);
	FRPGInventoryTabsTestAccess::ClickOther(*Widget);
	TestEqual(TEXT("Actual button selects other"), Widget->GetSelectedInventoryCategory(), EItemCategory::Craft);
	TestEqual(TEXT("Other 80 cells"), FRPGInventoryTabsTestAccess::Cells(*Widget), 80);
	Widget->SelectInventoryCategory(EItemCategory::None);
	TestEqual(TEXT("Invalid tab ignored"), Widget->GetSelectedInventoryCategory(), EItemCategory::Craft);
	for (auto Category : {EItemCategory::Equip, EItemCategory::Consume, EItemCategory::Craft})
	{
		const auto Capacity = Widget->GetInventoryViewModel()->GetInventoryCapacityForCategory(Category);
		TestEqual(TEXT("Independent displayed capacity"), Capacity.TotalSlots, 80);
		TestEqual(TEXT("Independent empty capacity"), Capacity.FreeSlots, 80);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryTabsPendingTest, "ProjectRPG.Item.InventoryTabs.PendingUI", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryTabsPendingTest::RunTest(const FString&)
{
	auto* Model = NewObject<URPGInventoryProjectionViewModel>();
	const FGuid Request = FGuid::NewGuid(), Item = FGuid::NewGuid();
	FRPGInventoryTabsTestAccess::Pending(*Model, Request, Item);
	TestEqual(TEXT("Sort reserves UI"), Model->PendingItemCommandCount, 1);
	FRPGInventoryTabsTestAccess::Ack(*Model, Request, ERPGItemCommandResultCode::Uncertain);
	TestEqual(TEXT("Uncertain keeps reservations"), Model->PendingItemCommandCount, 1);
	FRPGInventoryTabsTestAccess::Ack(*Model, Request, ERPGItemCommandResultCode::Succeeded);
	TestEqual(TEXT("Success ACK alone cannot release before replicated revisions"), Model->PendingItemCommandCount, 1);
	FRPGInventoryTabsTestAccess::Ack(*Model, Request, ERPGItemCommandResultCode::NoChange);
	TestEqual(TEXT("No-change releases without waiting for nonexistent mutations"), Model->PendingItemCommandCount, 0);
	return true;
}
#endif
