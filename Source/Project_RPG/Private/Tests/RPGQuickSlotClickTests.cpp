#if WITH_DEV_AUTOMATION_TESTS

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Input/DragAndDrop.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Misc/AutomationTest.h"
#include "UI/Inventory/Projection/RPGItemIdDragDropOperation.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UI/RPGQuickSlotWidget.h"
#include "UObject/Package.h"
#include "Widgets/SWidget.h"

struct FRPGQuickSlotClickTestAccess
{
	static void Bind(URPGQuickSlotWidget& Widget, UQuickSlotComponent& Component)
	{
		Widget.UnbindFromQuickSlot();
		Widget.LinkedQuickSlotComponent = &Component;
		Widget.BindItemCommandState();
		Component.OnItemSlotChanged.AddUniqueDynamic(&Widget, &URPGQuickSlotWidget::HandleSlotChanged);
		Widget.HandleSlotChanged(Widget.SlotIndex, *Component.GetItemSlotContent(Widget.SlotIndex));
	}
	static void SetSkillSlot(URPGQuickSlotWidget& Widget, bool bSkill) { Widget.bIsSkillSlot = bSkill; }
	static void Deactivate(URPGQuickSlotWidget& Widget) { Widget.NativeOnDeactivated(); }
	static bool IsPressed(const URPGQuickSlotWidget& Widget) { return Widget.PressedItemId.IsValid(); }
	static UImage* AddIcon(URPGQuickSlotWidget& Widget)
	{
		Widget.SlotItemImage = NewObject<UImage>(&Widget);
		Widget.ApplyItemIcon(NewObject<UTexture2D>(&Widget));
		return Widget.SlotItemImage;
	}
};

namespace RPGQuickSlotClickTests
{
FPointerEvent Pointer(FVector2D Position, FKey Button, bool bLeftHeld, uint32 PointerIndex = 0)
{
	TSet<FKey> Buttons;
	if (bLeftHeld) Buttons.Add(EKeys::LeftMouseButton);
	return FPointerEvent(PointerIndex, Position, Position, Buttons, Button, 0.f, FModifierKeysState());
}

struct FFixture
{
	FRPGItemActionPolicyRegistry Policies;
	FRPGItemOwnerRef Owner;
	UWorld* World = nullptr;
	ARPGPlayerController* Controller = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	UQuickSlotComponent* QuickSlots = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	URPGQuickSlotWidget* Widget = nullptr;
	TSharedPtr<SWidget> SlateWidget;
	FGuid ItemId = FGuid::NewGuid();
	FGuid OtherItemId = FGuid::NewGuid();
	FPrimaryAssetId DefinitionId{TEXT("RPGItemDefinition"), TEXT("ClickPotion")};
	FGeometry Geometry = FGeometry::MakeRoot(FVector2D(64.f, 64.f), FSlateLayoutTransform(1.f));
	FVector2D Inside{20.f, 20.f};
	FVector2D Outside{80.f, 20.f};

	FFixture()
	{
		Owner.Type = ERPGItemOwnerType::Character;
		Owner.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
		FRPGItemActionPolicy Policy;
		Policy.DefinitionId = DefinitionId;
		Policy.DefinitionVersion = 3;
		Policy.QuantityPerUse = 1;
		Policies.RegisterPolicy(Policy);
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<ARPGPlayerController>();
		APawn* Pawn = World->SpawnActor<APawn>();
		if (!Controller || !Pawn) return;
		ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
		Controller->Player = LocalPlayer;
		LocalPlayer->PlayerController = Controller;
		// SetOwningPlayer rejects controllers not marked as local, even with a ULocalPlayer pointer.
		Controller->SetAsLocalPlayerController();
		// This lightweight world has not initialized actors for play. PlayerContext resolves via its controller list.
		World->AddController(Controller);
		Controller->Possess(Pawn);
		Projection = Controller->FindComponentByClass<URPGInventoryProjectionComponent>();
		if (!Projection) return;
		QuickSlots = NewObject<UQuickSlotComponent>(Pawn);
		Pawn->AddInstanceComponent(QuickSlots);
		QuickSlots->RegisterComponent();
		QuickSlots->BindInventoryProjection(Projection, &Policies);
		Model = Controller->GetInventoryProjectionViewModel();
		if (!Model) return;
		if (!Projection->ApplyAuthoritativeRecords(Owner, {Record(ItemId, 0), Record(OtherItemId, 1)})) return;
		if (!Projection->ApplyCompletedRewardSnapshot(Owner, {Record(ItemId, 0), Record(OtherItemId, 1)},
			Projection->GetAuthoritativeSnapshotGeneration())) return;
		QuickSlots->SetItemSlotById(0, ItemId);
		Widget = NewObject<URPGQuickSlotWidget>(GetTransientPackage());
		Widget->SetOwningPlayer(Controller);
		if (!Widget->Initialize()) { Widget = nullptr; return; }
		// Retain the Slate owner: dropping this reference immediately routes NativeDestruct.
		SlateWidget = Widget->TakeWidget();
		// A lightweight APawn fixture avoids constructing the unrelated player/ability UI.
		FRPGQuickSlotClickTestAccess::Bind(*Widget, *QuickSlots);
	}

	~FFixture()
	{
		if (Widget) Widget->NativeDestruct();
		SlateWidget.Reset();
		if (Model) Model->InitializeFromPlayerController(nullptr);
		if (QuickSlots) QuickSlots->BindInventoryProjection(nullptr);
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}

	FRPGItemRecord Record(FGuid Id, int32 Slot, int32 Quantity = 3, int64 Revision = 9007199254740993LL) const
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(Id, 17, Quantity, {}, {}, State);
		FRPGItemLocation Location;
		Location.ContainerType = ERPGItemContainerType::Inventory;
		Location.ContainerId = Owner.OwnerId;
		Location.SlotIndex = Slot;
		FRPGItemRecord Result;
		FRPGItemRecord::TryRestore(DefinitionId, 3, Owner, Location, State, Revision,
			ERPGItemLifecycleState::Active, {}, Result);
		return Result;
	}
	FReply Press() { return Widget->NativeOnMouseButtonDown(Geometry, Pointer(Inside, EKeys::LeftMouseButton, true)); }
	FReply Release() { return Widget->NativeOnMouseButtonUp(Geometry, Pointer(Inside, EKeys::LeftMouseButton, false)); }
	FGuid LastRequest() const { return Model->LastCommandResult.RequestId; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuickSlotClickUseTest,
	"ProjectRPG.Item.QuickSlot.Click.UsesOnceOnMatchingRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGQuickSlotClickUseTest::RunTest(const FString&)
{
	using namespace RPGQuickSlotClickTests;
	FFixture F;
	if (!TestNotNull(TEXT("Item quickslot widget"), F.Widget)) return false;
	TestTrue(TEXT("Fixture preserves the owning controller"), F.Widget->GetOwningPlayer() == F.Controller);
	TestEqual(TEXT("Displayed ID matches assignment"), F.Widget->GetSlotItemId(), F.ItemId);
	F.Release();
	TestFalse(TEXT("An unpaired release (including drop release) cannot consume"), F.LastRequest().IsValid());
	TestTrue(TEXT("Left press is handled and requests capture"), F.Press().GetMouseCaptor().IsValid());
	if (!TestTrue(TEXT("A real pending item click is armed"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget))) return false;
	TestFalse(TEXT("Press does not consume"), F.LastRequest().IsValid());
	F.Widget->NativeOnMouseButtonUp(F.Geometry, Pointer(F.Inside, EKeys::RightMouseButton, true));
	F.Widget->NativeOnMouseButtonUp(F.Geometry, Pointer(F.Inside, EKeys::LeftMouseButton, false, 1));
	TestFalse(TEXT("Wrong button or pointer cannot consume"), F.LastRequest().IsValid());
	TestTrue(TEXT("Matching release is handled"), F.Release().IsEventHandled());
	const FGuid FirstRequest = F.LastRequest();
	TestTrue(TEXT("Matching click reaches the item command facade"), FirstRequest.IsValid());
	TestEqual(TEXT("Only consume is requested"), F.Model->LastCommandResult.Operation, FName(TEXT("Item.Consume")));
	TestEqual(TEXT("No authentication means no successful consumption"), F.Model->LastCommandResult.Result,
		ERPGItemCommandResultCode::ServerStateError);
	F.Release();
	TestEqual(TEXT("A repeated release cannot resubmit"), F.LastRequest(), FirstRequest);
	F.Widget->NativeOnMouseButtonDoubleClick(F.Geometry, Pointer(F.Inside, EKeys::LeftMouseButton, true));
	TestEqual(TEXT("Double-click press alone cannot consume"), F.LastRequest(), FirstRequest);
	F.Release();
	TestTrue(TEXT("The second valid click submits once on release"), F.LastRequest().IsValid() && F.LastRequest() != FirstRequest);
	FRPGInventoryProjectionEntry Entry;
	TestTrue(TEXT("The item remains projected without a successful receipt"), F.Projection->FindProjectedItem(F.ItemId, Entry));
	TestEqual(TEXT("The UI never reduces quantity optimistically"), Entry.GetQuantity(), 3);
	TestEqual(TEXT("The revision is not truncated or changed"), Entry.GetRevision(), 9007199254740993LL);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuickSlotClickCancelTest,
	"ProjectRPG.Item.QuickSlot.Click.CancelsOutsideDragAndCaptureLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGQuickSlotClickCancelTest::RunTest(const FString&)
{
	using namespace RPGQuickSlotClickTests;
	FFixture F;
	if (!TestNotNull(TEXT("Item quickslot widget"), F.Widget)) return false;
	F.Press();
	if (!TestTrue(TEXT("Cancellation starts from an armed item click"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget))) return false;
	F.Widget->NativeOnMouseButtonUp(F.Geometry, Pointer(F.Outside, EKeys::LeftMouseButton, false));
	F.Release();
	TestFalse(TEXT("Release outside cancels permanently"), F.LastRequest().IsValid());
	F.Press();
	F.Widget->NativeOnMouseMove(F.Geometry, Pointer(F.Outside, EKeys::LeftMouseButton, true));
	F.Widget->NativeOnMouseMove(F.Geometry, Pointer(F.Inside, EKeys::LeftMouseButton, true));
	F.Release();
	TestFalse(TEXT("Leaving and returning cannot become a click"), F.LastRequest().IsValid());
	F.Press();
	const FVector2D DragPosition = F.Inside + FVector2D(FSlateApplication::Get().GetDragTriggerDistance() + 1.f, 0.f);
	F.Widget->NativeOnMouseMove(F.Geometry, Pointer(DragPosition, EKeys::LeftMouseButton, true));
	F.Release();
	TestFalse(TEXT("Drag-threshold movement within the cell cancels"), F.LastRequest().IsValid());
	F.Press();
	UDragDropOperation* Operation = nullptr;
	F.Widget->NativeOnDragDetected(F.Geometry, Pointer(F.Inside, EKeys::LeftMouseButton, true), Operation);
	F.Release();
	TestFalse(TEXT("Drag detection never consumes"), F.LastRequest().IsValid());
	F.Press();
	F.Widget->NativeOnDragCancelled(FDragDropEvent(Pointer(F.Inside, EKeys::LeftMouseButton, false), nullptr), nullptr);
	F.Release();
	TestFalse(TEXT("Cancelled drag never consumes"), F.LastRequest().IsValid());
	F.Press();
	F.Widget->NativeOnMouseCaptureLost(FCaptureLostEvent(0, 0));
	F.Release();
	TestFalse(TEXT("Capture loss cancels"), F.LastRequest().IsValid());
	F.Press();
	F.Widget->NativeOnMouseMove(F.Geometry, Pointer(F.Inside, EKeys::Invalid, false));
	F.Release();
	TestFalse(TEXT("Missed release cancels on the next move"), F.LastRequest().IsValid());
	F.Press();
	FRPGQuickSlotClickTestAccess::Deactivate(*F.Widget);
	F.Release();
	TestFalse(TEXT("Deactivation cancels"), F.LastRequest().IsValid());
	F.Press();
	F.Widget->NativeDestruct();
	FRPGQuickSlotClickTestAccess::Bind(*F.Widget, *F.QuickSlots);
	F.Release();
	TestFalse(TEXT("Destruction and rebind do not retain a press"), F.LastRequest().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuickSlotClickAssignmentTest,
	"ProjectRPG.Item.QuickSlot.Click.PreservesAssignmentDropAndSkillBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGQuickSlotClickAssignmentTest::RunTest(const FString&)
{
	using namespace RPGQuickSlotClickTests;
	FFixture F;
	if (!TestNotNull(TEXT("Item quickslot widget"), F.Widget)) return false;
	F.Press();
	if (!TestTrue(TEXT("Assignment checks start from an armed item click"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget))) return false;
	F.QuickSlots->SetItemSlotById(0, F.OtherItemId);
	F.QuickSlots->SetItemSlotById(0, F.ItemId);
	F.Release();
	TestFalse(TEXT("Replacing and restoring an assignment invalidates the old press"), F.LastRequest().IsValid());
	F.Press();
	F.Widget->SetSlotIndex(1);
	F.Release();
	TestFalse(TEXT("Changing the slot index invalidates the click"), F.LastRequest().IsValid());
	F.Widget->SetSlotIndex(0);
	F.Press();
	F.Widget->SetIsEnabled(false);
	F.Release();
	TestFalse(TEXT("Disabled cells cannot consume"), F.LastRequest().IsValid());
	F.Widget->SetIsEnabled(true);
	FRPGQuickSlotClickTestAccess::SetSkillSlot(*F.Widget, true);
	F.Press();
	F.Release();
	F.Widget->UseAssignedItem();
	TestFalse(TEXT("Skill cells do not submit item commands"), F.LastRequest().IsValid());
	TestFalse(TEXT("Skill press does not arm an item click"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget));
	FRPGQuickSlotClickTestAccess::SetSkillSlot(*F.Widget, false);
	F.Press();
	URPGItemIdDragDropOperation* Drag = NewObject<URPGItemIdDragDropOperation>();
	Drag->ItemId = F.OtherItemId;
	Drag->bSplitStack = true;
	TestFalse(TEXT("A split gesture cannot register a shortcut instead"), F.Widget->NativeOnDrop(F.Geometry,
		FDragDropEvent(Pointer(F.Inside, EKeys::LeftMouseButton, false), nullptr), Drag));
	Drag->bSplitStack = false;
	Drag->bChooseSplitQuantity = true;
	TestFalse(TEXT("Quantity selection cannot fall back to shortcut registration"), F.Widget->NativeOnDrop(F.Geometry,
		FDragDropEvent(Pointer(F.Inside, EKeys::LeftMouseButton, false), nullptr), Drag));
	Drag->bChooseSplitQuantity = false;
	TestTrue(TEXT("Existing owned ItemId drop still registers"), F.Widget->NativeOnDrop(F.Geometry,
		FDragDropEvent(Pointer(F.Inside, EKeys::LeftMouseButton, false), nullptr), Drag));
	F.Release();
	TestFalse(TEXT("Dropping an item cannot consume the new assignment"), F.LastRequest().IsValid());
	TestEqual(TEXT("Drop updates the assigned ID"), F.QuickSlots->GetItemSlotContent(0)->ItemId, F.OtherItemId);
	F.Press();
	TestTrue(TEXT("Quantity-only projection changes remain valid"), F.Projection->ApplyAuthoritativeMutationRecords(
		F.Owner, {F.Record(F.OtherItemId, 1, 2, 9007199254740994LL)}));
	F.Release();
	TestTrue(TEXT("The same assignment can use the current projection after a quantity change"), F.LastRequest().IsValid());
	FRPGInventoryProjectionEntry Entry;
	TestTrue(TEXT("Quantity changes are read from the projection"), F.Projection->FindProjectedItem(F.OtherItemId, Entry));
	TestEqual(TEXT("The click cannot optimistically mutate the updated quantity"), Entry.GetQuantity(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuickSlotCommandPresentationTest,
	"ProjectRPG.Item.QuickSlot.Click.SharesRecoveryPresentationWithoutDisablingDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGQuickSlotCommandPresentationTest::RunTest(const FString&)
{
	using namespace RPGQuickSlotClickTests;
	FFixture F;
	if (!TestNotNull(TEXT("Item quickslot widget"), F.Widget)) return false;
	TestTrue(TEXT("Quickslot uses the controller-owned window model"),
		F.Model == F.Controller->GetInventoryProjectionViewModel());
	UImage* Icon = FRPGQuickSlotClickTestAccess::AddIcon(*F.Widget);
	TestEqual(TEXT("Ready icon is fully visible"), Icon->GetColorAndOpacity().A, 1.f);
	F.Press();
	if (!TestTrue(TEXT("Ready item arms a click"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget))) return false;
	F.Model->bHasInventoryRecovery = true;
	using Fields = URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor;
	F.Model->BroadcastFieldValueChanged(Fields::bHasInventoryRecovery);
	TestEqual(TEXT("Recovery notification dims the existing icon"), Icon->GetColorAndOpacity().A, 0.4f);
	TestFalse(TEXT("Recovery invalidates a click already being held"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget));
	TestFalse(TEXT("Recovery reason is visible without opening the bag"), F.Widget->GetToolTipText().IsEmpty());
	TestTrue(TEXT("The widget stays enabled for shortcut drag or clear"), F.Widget->GetIsEnabled());
	F.Release();
	F.QuickSlots->UseItemSlot(0, F.Controller);
	TestFalse(TEXT("Neither held click nor keyboard use bypasses recovery"), F.LastRequest().IsValid());
	const FReply BlockedPress = F.Press();
	TestTrue(TEXT("Recovery press stays in the UI"), BlockedPress.IsEventHandled());
	TestFalse(TEXT("Recovery press cannot arm consumption"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget));
	TestFalse(TEXT("Recovery press does not capture a consumption click"), BlockedPress.GetMouseCaptor().IsValid());
	F.Model->bHasInventoryRecovery = false;
	F.Model->BroadcastFieldValueChanged(Fields::bHasInventoryRecovery);
	TestEqual(TEXT("Recovery clearance restores the same icon"), Icon->GetColorAndOpacity().A, 1.f);
	F.Release();
	TestFalse(TEXT("Releasing a press that began during recovery cannot consume after recovery"), F.LastRequest().IsValid());
	F.Model->LoadState = ERPGInventoryProjectionLoadState::Loading;
	F.Model->BroadcastFieldValueChanged(Fields::LoadState);
	F.Press();
	TestFalse(TEXT("Loading press cannot arm consumption"), FRPGQuickSlotClickTestAccess::IsPressed(*F.Widget));
	F.Model->LoadState = ERPGInventoryProjectionLoadState::Ready;
	F.Model->BroadcastFieldValueChanged(Fields::LoadState);
	F.Release();
	TestFalse(TEXT("Load completion does not turn a blocked press into a use"), F.LastRequest().IsValid());
	F.Press(); F.Release();
	TestTrue(TEXT("A new click reaches the shared command route"), F.LastRequest().IsValid());
	F.Widget->NativeDestruct();
	F.Model->bHasInventoryRecovery = true;
	F.Model->BroadcastFieldValueChanged(Fields::bHasInventoryRecovery);
	TestEqual(TEXT("Destruction removes the notification binding"), Icon->GetColorAndOpacity().A, 1.f);
	return true;
}

#endif
