#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/Inventory/Projection/RPGItemIdDragDropOperation.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

// Native-only fixture. No Slate drawing, backend auth, HTTP or durable writes.
struct FRPGInventorySplitQuantityTestFixture
{
	FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, TEXT("11111111-2222-3333-4444-555555555555")};
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	URPGInventoryProjectionWidget* Screen = nullptr;
	FRPGItemRecord Source;
	bool Ready = false;
	FRPGInventorySplitQuantityTestFixture()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>();
		if (!Controller) return;
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection); Projection->RegisterComponent();
		auto* Commands = NewObject<URPGItemCommandComponent>(Controller);
		Controller->AddInstanceComponent(Commands); Commands->RegisterComponent();
		Model = NewObject<URPGInventoryProjectionViewModel>(Controller);
		Model->InitializeFromPlayerController(Controller);
		Source = MakeRecord(7, 0);
		if (!Projection->ApplyAuthoritativeRecords(Owner, {Source}) || !Projection->ApplyCompletedRewardSnapshot(
			Owner, {Source}, Projection->GetAuthoritativeSnapshotGeneration())) return;
		Screen = NewObject<URPGInventoryProjectionWidget>(Controller);
		Screen->WidgetTree = NewObject<UWidgetTree>(Screen);
		auto* Rows = Screen->WidgetTree->ConstructWidget<UVerticalBox>();
		Screen->WidgetTree->RootWidget = Rows;
		Screen->InventoryViewModel = Model;
		Screen->LoadStateText = Screen->WidgetTree->ConstructWidget<UTextBlock>();
		Screen->CommandResultText = Screen->WidgetTree->ConstructWidget<UTextBlock>();
		Screen->InventoryGrid = Screen->WidgetTree->ConstructWidget<UUniformGridPanel>();
		Rows->AddChildToVerticalBox(Screen->LoadStateText);
		Rows->AddChildToVerticalBox(Screen->InventoryGrid);
		Rows->AddChildToVerticalBox(Screen->CommandResultText);
		Ready = Screen->EnsureSplitQuantityControls();
		Screen->BindViewModel();
	}
	~FRPGInventorySplitQuantityTestFixture()
	{
		if (Screen) { Screen->CancelSplitQuantitySelection(); Screen->UnbindViewModel(); }
		if (Model) Model->InitializeFromPlayerController(nullptr);
		if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	FRPGItemRecord MakeRecord(int32 Quantity, int32 Slot) const
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 37, Quantity, {}, {}, State);
		FRPGItemRecord Record;
		FRPGItemRecord::TryRestore({TEXT("RPGItemDefinition"), TEXT("QuantityPotion")}, 1, Owner,
			{ERPGItemContainerType::Inventory, Owner.OwnerId, Slot}, State, 9007199254740993LL,
			ERPGItemLifecycleState::Active, {}, Record);
		return Record;
	}
	bool Open() const { return Ready && Screen->OpenSplitQuantitySelection(Source.GetItemId(), Source.GetRevision(), 3); }
	void Text(const TCHAR* Value) const
	{
		Screen->SplitQuantityInput->SetText(FText::FromString(Value));
		Screen->SplitQuantityInput->OnTextChanged.Broadcast(Screen->SplitQuantityInput->GetText());
	}
	int32 Read() const { int32 Value = 0; Screen->TryReadSplitQuantity(Value); return Value; }
	bool Escape() const
	{
		return Screen->NativeOnPreviewKeyDown(FGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled();
	}
	void Deactivate() const { Screen->NativeOnDeactivated(); }
	void Loading() const { Model->LoadState = ERPGInventoryProjectionLoadState::Loading; Screen->RefreshFromViewModel(); }
	URPGInventoryProjectionWidget* UnsupportedScreen() const
	{
		auto* Other = NewObject<URPGInventoryProjectionWidget>(Controller);
		Other->WidgetTree = NewObject<UWidgetTree>(Other); Other->InventoryViewModel = Model;
		Other->InventoryGrid = Other->WidgetTree->ConstructWidget<UUniformGridPanel>();
		return Other;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventorySplitQuantityValidationTest,
	"ProjectRPG.Item.SplitQuantity.WholeIntegerValidationAndSingleSubmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventorySplitQuantityValidationTest::RunTest(const FString&)
{
	FRPGInventorySplitQuantityTestFixture F;
	if (!TestTrue(TEXT("Native inline selection opens"), F.Open())) return false;
	TestFalse(TEXT("Opening has no command"), F.Model->bHasLastCommandResult);
	TestEqual(TEXT("Default is floor half"), F.Read(), 3);
	for (const TCHAR* Invalid : {TEXT(""), TEXT("0"), TEXT("7"), TEXT("8"), TEXT("-1"), TEXT("+1"),
		TEXT("1.5"), TEXT("1e0"), TEXT("1,000"), TEXT(" 1"), TEXT("1 "), TEXT("99999999999999999999")})
	{
		F.Text(Invalid);
		TestFalse(TEXT("Malformed/out-of-range input disables confirmation"), F.Screen->CanConfirmSplitQuantitySelection());
		FGuid RequestId = FGuid::NewGuid();
		TestFalse(TEXT("Invalid confirmation submits nothing"), F.Screen->ConfirmSplitQuantitySelection(RequestId));
		TestFalse(TEXT("No request identity for invalid input"), RequestId.IsValid());
		TestTrue(TEXT("Invalid input can be corrected without silently closing"), F.Screen->IsSplitQuantitySelectionOpen());
	}
	TestFalse(TEXT("Invalid input sent no item command"), F.Model->bHasLastCommandResult);
	F.Text(TEXT("2")); FGuid RequestId;
	TestTrue(TEXT("Exact chosen quantity submits"), F.Screen->ConfirmSplitQuantitySelection(RequestId));
	TestTrue(TEXT("Submitted identity"), RequestId.IsValid());
	TestEqual(TEXT("Uses the existing Split facade"), F.Model->LastCommandResult.Operation, FName(TEXT("Item.Split")));
	TestFalse(TEXT("Closes before submission callbacks"), F.Screen->IsSplitQuantitySelectionOpen());
	const auto Submitted = F.Model->LastCommandResult.RequestId;
	TestFalse(TEXT("Double confirmation cannot submit again"), F.Screen->ConfirmSplitQuantitySelection(RequestId));
	TestEqual(TEXT("No second command identity"), F.Model->LastCommandResult.RequestId, Submitted);
	FRPGInventoryProjectionEntry Original; F.Projection->FindProjectedItem(F.Source.GetItemId(), Original);
	TestEqual(TEXT("No optimistic quantity mutation"), Original.GetQuantity(), 7);
	return true; // unauthenticated transient owner rejects the RPC; not a successful backend commit
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventorySplitQuantitySnapshotTest,
	"ProjectRPG.Item.SplitQuantity.SourceDestinationAndLoadChangesInvalidateSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventorySplitQuantitySnapshotTest::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 4; ++Case)
	{
		FRPGInventorySplitQuantityTestFixture F;
		if (!TestTrue(TEXT("Open captured snapshot"), F.Open())) return false;
		if (Case == 0) F.Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source.CopyWithRevision(F.Source.GetRevision() + 1)});
		if (Case == 1) F.Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source, F.MakeRecord(1, 3)});
		if (Case == 2) F.Projection->ApplyAuthoritativeRecords(F.Owner, {});
		if (Case == 3) F.Loading();
		TestFalse(TEXT("Changed snapshot closes selection without a newer-revision retry"), F.Screen->IsSplitQuantitySelectionOpen());
		FGuid RequestId;
		TestFalse(TEXT("Obsolete selection cannot confirm"), F.Screen->ConfirmSplitQuantitySelection(RequestId));
		TestFalse(TEXT("No automatic request after invalidation"), F.Model->bHasLastCommandResult);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventorySplitQuantityCancelTest,
	"ProjectRPG.Item.SplitQuantity.CancelEscapeCloseAndUnsupportedLayoutSendNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventorySplitQuantityCancelTest::RunTest(const FString&)
{
	FRPGInventorySplitQuantityTestFixture F;
	if (!TestTrue(TEXT("Open selection"), F.Open())) return false;
	F.Screen->CancelSplitQuantitySelection();
	TestFalse(TEXT("Cancel closes selection"), F.Screen->IsSplitQuantitySelectionOpen());
	TestTrue(TEXT("Can reopen"), F.Open());
	TestTrue(TEXT("First Escape is consumed by selection"), F.Escape());
	TestFalse(TEXT("Escape closes selection"), F.Screen->IsSplitQuantitySelectionOpen());
	TestTrue(TEXT("Reopen before window teardown"), F.Open()); F.Deactivate();
	TestFalse(TEXT("Window deactivation cancels"), F.Screen->IsSplitQuantitySelectionOpen());
	TestFalse(TEXT("Stale revision cannot open"), F.Screen->OpenSplitQuantitySelection(F.Source.GetItemId(), F.Source.GetRevision()+1, 3));
	TestFalse(TEXT("Unsupported BP layout fails closed"), F.UnsupportedScreen()->OpenSplitQuantitySelection(F.Source.GetItemId(), F.Source.GetRevision(), 3));
	TestFalse(TEXT("No cancellation/layout fallback command"), F.Model->bHasLastCommandResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventorySplitQuantityInputTest,
	"ProjectRPG.Item.SplitQuantity.CtrlDropOpensSelectionWithoutImmediateSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventorySplitQuantityInputTest::RunTest(const FString&)
{
	FRPGInventorySplitQuantityTestFixture F;
	if (!TestTrue(TEXT("Native fixture ready"), F.Ready)) return false;
	auto* Source = NewObject<URPGInventoryProjectionItemSlotWidget>(F.Screen->WidgetTree);
	auto* Empty = NewObject<URPGInventoryProjectionItemSlotWidget>(F.Screen->WidgetTree);
	Source->SetItem(F.Source.GetItemId(), FText::FromString(TEXT("Potion")), F.Model, false, 0);
	Empty->SetItem(FGuid(), FText::GetEmpty(), F.Model, false, 3);
	const FPointerEvent Pointer(0, FSlateApplication::CursorPointerIndex, FVector2D::ZeroVector, FVector2D::ZeroVector,
		TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.f, FModifierKeysState(false,false,true,false,false,false,false,false,false));
	UDragDropOperation* Operation = nullptr; Source->NativeOnDragDetected(FGeometry(), Pointer, Operation);
	auto* Drag = Cast<URPGItemIdDragDropOperation>(Operation);
	if (!TestNotNull(TEXT("Ctrl drag payload"), Drag)) return false;
	TestTrue(TEXT("Ctrl is an explicit split intent"), Drag->bSplitStack && Drag->bChooseSplitQuantity);
	TestFalse(TEXT("Occupied target cannot merge/swap"), Source->NativeOnDrop(FGeometry(), FDragDropEvent(Pointer,nullptr), Drag));
	TestTrue(TEXT("Empty drop opens native parent controls"), Empty->NativeOnDrop(FGeometry(), FDragDropEvent(Pointer,nullptr), Drag));
	TestTrue(TEXT("Selection is open"), F.Screen->IsSplitQuantitySelectionOpen());
	TestFalse(TEXT("Drop alone never submits half or whole quantity"), F.Model->bHasLastCommandResult);
	TestFalse(TEXT("Second open cannot overwrite the first captured intent"), F.Screen->OpenSplitQuantitySelection(F.Source.GetItemId(), F.Source.GetRevision(), 4));
	F.Screen->CancelSplitQuantitySelection();
	F.Source = F.MakeRecord(1, 0); F.Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source});
	Source->SetItem(F.Source.GetItemId(), FText::GetEmpty(), F.Model, false, 0);
	Operation = nullptr; Source->NativeOnDragDetected(FGeometry(), Pointer, Operation);
	TestNull(TEXT("Single item does not become a whole-stack Ctrl move"), Operation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventorySplitQuantityPrecisionTest,
	"ProjectRPG.Item.SplitQuantity.Int32QuantityRemainsExactWithoutFloatRounding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventorySplitQuantityPrecisionTest::RunTest(const FString&)
{
	FRPGInventorySplitQuantityTestFixture F;
	if (!TestTrue(TEXT("Native fixture ready"), F.Ready)) return false;
	F.Source = F.MakeRecord(MAX_int32, 0); F.Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source});
	if (!TestTrue(TEXT("Open synthetic large quantity"), F.Open())) return false;
	F.Text(TEXT("2147483646"));
	TestTrue(TEXT("Exact positive integer below source"), F.Screen->CanConfirmSplitQuantitySelection());
	TestEqual(TEXT("No float rounding"), F.Read(), MAX_int32 - 1);
	F.Text(TEXT("2147483647")); TestFalse(TEXT("Cannot split all"), F.Screen->CanConfirmSplitQuantitySelection());
	F.Text(TEXT("9999999999")); TestFalse(TEXT("Integer overflow is rejected"), F.Screen->CanConfirmSplitQuantitySelection());
	TestFalse(TEXT("Parser fixture does not authorize an over-cap backend stack"), F.Model->bHasLastCommandResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventorySplitQuantityAuthoredLayoutTest,
	"ProjectRPG.Item.SplitQuantity.AuthoredBagHasNativeSelectionAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventorySplitQuantityAuthoredLayoutTest::RunTest(const FString&)
{
	const auto* Class = Cast<UWidgetBlueprintGeneratedClass>(LoadClass<URPGInventoryProjectionWidget>(nullptr,
		TEXT("/Game/Blueprints/UI/Inventory/Projection/WBP_InventoryProjection.WBP_InventoryProjection_C")));
	if (!TestNotNull(TEXT("Actual authored bag class"), Class)) return false;
	const auto* Tree = Class->GetWidgetTreeArchetype();
	if (!TestNotNull(TEXT("Read-only bag template; no painting or BP callbacks"), Tree)) return false;
	TestNotNull(TEXT("Authored grid uses native cells"), Cast<UUniformGridPanel>(Tree->FindWidget(TEXT("InventoryGrid"))));
	const auto* Result = Tree->FindWidget(TEXT("CommandResultText"));
	const auto* State = Tree->FindWidget(TEXT("LoadStateText"));
	TestTrue(TEXT("Native selection can attach without editing the saved WBP"),
		(Result && Cast<UVerticalBox>(Result->GetParent())) || (State && Cast<UVerticalBox>(State->GetParent())));
	return true;
}
#endif
