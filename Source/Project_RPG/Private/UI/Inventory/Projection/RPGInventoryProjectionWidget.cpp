#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Controller/RPGPlayerController.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UI/RPGConfirmationInput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGInventoryProjectionWidget)

namespace
{
FString GetEnumDisplayName(const UEnum* Enum, const int64 Value)
{
	return Enum
		? Enum->GetDisplayNameTextByValue(Value).ToString()
		: FString::FromInt(static_cast<int32>(Value));
}

FString GetShortItemId(const FGuid& ItemId)
{
	const FString FullId = ItemId.ToString(EGuidFormats::Digits);
	return FullId.Left(8);
}

FText GetItemDisplayName(const FRPGInventoryProjectionEntry& Entry,
	const URPGInventoryProjectionViewModel* ViewModel)
{
	return ViewModel
		? ViewModel->GetItemPresentation(Entry.GetItemId()).DisplayName
		: FText::FromName(Entry.GetDefinitionId().PrimaryAssetName);
}

UBorder* MakeSlotFrame(
	UWidgetTree* Tree,
	const FString& Label,
	const bool bOccupied)
{
	UBorder* Frame = Tree->ConstructWidget<UBorder>();
	Frame->SetBrushColor(bOccupied
		? FLinearColor(0.27f, 0.16f, 0.08f, 1.f)
		: FLinearColor(0.055f, 0.075f, 0.10f, 1.f));
	Frame->SetPadding(FMargin(3.f));
	Frame->SetHorizontalAlignment(HAlign_Center);
	Frame->SetVerticalAlignment(VAlign_Center);

	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetJustification(ETextJustify::Center);
	Text->SetAutoWrapText(true);
	Text->SetColorAndOpacity(bOccupied
		? FSlateColor(FLinearColor(1.f, 0.82f, 0.47f))
		: FSlateColor(FLinearColor(0.55f, 0.62f, 0.69f)));
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 10;
	Text->SetFont(Font);
	Frame->SetContent(Text);
	return Frame;
}

void FillInventoryGrid(
	UWidgetTree* Tree,
	UUniformGridPanel* Grid,
	const TArray<FRPGInventoryProjectionEntry>& Entries,
	URPGInventoryProjectionViewModel* ViewModel, EItemCategory Category)
{
	Grid->ClearChildren();
	Grid->SetMinDesiredSlotWidth(61.f);
	Grid->SetMinDesiredSlotHeight(61.f);
	Grid->SetSlotPadding(FMargin(2.f));

	constexpr int32 Columns = 10;
	const int32 SlotCount = RPGItemInventoryPolicy::SlotsPerTab;
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		const int32 StorageSlot = RPGItemInventoryPolicy::FirstSlot(Category) + Index;
		const FRPGInventoryProjectionEntry* Entry = Entries.FindByPredicate(
			[StorageSlot](const FRPGInventoryProjectionEntry& Candidate)
			{ return Candidate.GetSlotIndex() == StorageSlot; });
		const FText Label = Entry
			? FText::Format(NSLOCTEXT("RPGItemUI", "InventoryCellLabel", "{0}\nx{1}\n[{2}]"),
				GetItemDisplayName(*Entry, ViewModel), FText::AsNumber(Entry->GetQuantity()),
				FText::AsNumber(Index + 1))
			: FText::GetEmpty();
		UWidget* Cell = nullptr;
		URPGInventoryProjectionItemSlotWidget* ItemCell =
			Tree->ConstructWidget<URPGInventoryProjectionItemSlotWidget>();
		if (ItemCell)
		{
			ItemCell->SetItem(Entry ? Entry->GetItemId() : FGuid(), Label, ViewModel, false, StorageSlot);
			Cell = ItemCell;
		}
		if (!Cell)
		{
			Cell = MakeSlotFrame(Tree, Label.ToString(), Entry != nullptr);
		}
		UUniformGridSlot* Slot = Grid->AddChildToUniformGrid(
			Cell,
			Index / Columns,
			Index % Columns);
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}
}

FString GetEquipmentSlotLabel(const EEquipmentSlotType Slot)
{
	switch (Slot)
	{
	case EEquipmentSlotType::Head: return TEXT("HEAD");
	case EEquipmentSlotType::Chest: return TEXT("CHEST");
	case EEquipmentSlotType::Legs: return TEXT("LEGS");
	case EEquipmentSlotType::Feet: return TEXT("FEET");
	case EEquipmentSlotType::Hands: return TEXT("HANDS");
	case EEquipmentSlotType::Necklace: return TEXT("NECK");
	case EEquipmentSlotType::Ring_L: return TEXT("RING L");
	case EEquipmentSlotType::Ring_R: return TEXT("RING R");
	case EEquipmentSlotType::Weapon_Primary_L: return TEXT("MAIN L");
	case EEquipmentSlotType::Weapon_Primary_R: return TEXT("MAIN R");
	case EEquipmentSlotType::Weapon_Secondary_L: return TEXT("OFF L");
	case EEquipmentSlotType::Weapon_Secondary_R: return TEXT("OFF R");
	case EEquipmentSlotType::Utility_1: return TEXT("UTILITY 1");
	case EEquipmentSlotType::Utility_2: return TEXT("UTILITY 2");
	default: return TEXT("SLOT");
	}
}

template <size_t SlotCount>
void FillEquipmentColumn(
	UWidgetTree* Tree,
	UVerticalBox* Column,
	const EEquipmentSlotType (&Slots)[SlotCount],
	const TArray<FRPGInventoryProjectionEntry>& Entries,
	URPGInventoryProjectionViewModel* ViewModel)
{
	Column->ClearChildren();
	for (const EEquipmentSlotType EquipmentSlot : Slots)
	{
		const FRPGInventoryProjectionEntry* Entry = Entries.FindByPredicate(
			[EquipmentSlot](const FRPGInventoryProjectionEntry& Candidate)
			{
				return Candidate.GetSlotIndex() ==
					static_cast<int32>(EquipmentSlot);
			});
		const FText Label = Entry
			? FText::Format(NSLOCTEXT("RPGItemUI", "EquipmentCellLabel", "{0}\n{1}"),
				FText::FromString(GetEquipmentSlotLabel(EquipmentSlot)),
				GetItemDisplayName(*Entry, ViewModel))
			: FText::FromString(GetEquipmentSlotLabel(EquipmentSlot));
		USizeBox* Cell = Tree->ConstructWidget<USizeBox>();
		Cell->SetWidthOverride(84.f);
		Cell->SetHeightOverride(56.f);
		auto* ItemCell = Tree->ConstructWidget<URPGInventoryProjectionItemSlotWidget>();
		ItemCell->SetItem(Entry ? Entry->GetItemId() : FGuid(), Label, ViewModel, true,
			static_cast<int32>(EquipmentSlot));
		Cell->SetContent(ItemCell);
		UVerticalBoxSlot* Slot = Column->AddChildToVerticalBox(Cell);
		Slot->SetPadding(FMargin(2.f));
	}
}

void FillEquipmentSlots(
	UWidgetTree* Tree,
	UVerticalBox* Left,
	UVerticalBox* Right,
	const TArray<FRPGInventoryProjectionEntry>& Entries,
	URPGInventoryProjectionViewModel* ViewModel)
{
	constexpr EEquipmentSlotType LeftSlots[] = {
		EEquipmentSlotType::Head,
		EEquipmentSlotType::Chest,
		EEquipmentSlotType::Legs,
		EEquipmentSlotType::Feet,
		EEquipmentSlotType::Hands,
		EEquipmentSlotType::Weapon_Primary_L,
		EEquipmentSlotType::Weapon_Secondary_L};
	constexpr EEquipmentSlotType RightSlots[] = {
		EEquipmentSlotType::Necklace,
		EEquipmentSlotType::Ring_L,
		EEquipmentSlotType::Ring_R,
		EEquipmentSlotType::Weapon_Primary_R,
		EEquipmentSlotType::Weapon_Secondary_R,
		EEquipmentSlotType::Utility_1,
		EEquipmentSlotType::Utility_2};
	FillEquipmentColumn(Tree, Left, LeftSlots, Entries, ViewModel);
	FillEquipmentColumn(Tree, Right, RightSlots, Entries, ViewModel);
}

FText BuildProjectionText(
	const TArray<FRPGInventoryProjectionEntry>& Entries,
	const bool bEquipment)
{
	if (Entries.IsEmpty())
	{
		return FText::FromString(
			bEquipment
				? TEXT("No equipped records")
				: TEXT("No inventory records"));
	}

	TArray<FString> Lines;
	Lines.Reserve(Entries.Num());
	for (const FRPGInventoryProjectionEntry& Entry : Entries)
	{
		const FString Location = bEquipment
			? GetEnumDisplayName(
				StaticEnum<EEquipmentSlotType>(),
				Entry.GetSlotIndex())
			: FString::Printf(TEXT("Slot %d"), Entry.GetSlotIndex());
		Lines.Add(FString::Printf(
			TEXT("%s  |  %s  |  x%d  |  rev %lld  |  #%s"),
			*Location,
			*Entry.GetDefinitionId().ToString(),
			Entry.GetQuantity(),
			static_cast<long long>(Entry.GetRevision()),
			*GetShortItemId(Entry.GetItemId())));
	}

	return FText::FromString(FString::Join(Lines, TEXT("\n\n")));
}
}

void URPGInventoryProjectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Only the title text is draggable; its neighboring close button and item
	// controls keep their normal clicks.
	if (!TitleBar && WidgetTree)
	{
		TitleBar = WidgetTree->FindWidget(TEXT("TitleText"));
	}
	if (TitleBar)
	{
		TitleBar->SetVisibility(ESlateVisibility::Visible);
	}
	bCanDrag = false;

	UnbindViewModel();
	if (auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		InventoryViewModel = Controller->GetInventoryProjectionViewModel();
		bOwnsInventoryViewModel = false;
	}
	else
	{
		if (!InventoryViewModel || !bOwnsInventoryViewModel)
			InventoryViewModel = NewObject<URPGInventoryProjectionViewModel>(this);
		bOwnsInventoryViewModel = true;
		InventoryViewModel->InitializeFromPlayerController(GetOwningPlayer());
	}
	if (InventoryTabButton)
	{
		if (auto* Label = Cast<UTextBlock>(InventoryTabButton->GetContent()))
			Label->SetText(NSLOCTEXT("RPGItemUI", "OpenInventory", "Open Bag"));
		InventoryTabButton->OnClicked.AddUniqueDynamic(
			this, &ThisClass::HandleInventoryTabClicked);
	}
	if (EquipmentTabButton)
	{
		if (auto* Label = Cast<UTextBlock>(EquipmentTabButton->GetContent()))
			Label->SetText(NSLOCTEXT("RPGItemUI", "OpenEquipment", "Open Equipment"));
		EquipmentTabButton->OnClicked.AddUniqueDynamic(
			this, &ThisClass::HandleEquipmentTabClicked);
	}
	if (!MailTabButton && InventoryTabButton && InventoryTabButton->GetParent())
	{
		MailTabButton = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(NSLOCTEXT("RPGItemUI", "OpenRewardMail", "Open Mail"));
		MailTabButton->SetContent(Label);
		InventoryTabButton->GetParent()->AddChild(MailTabButton);
	}
	if (MailTabButton)
	{
		MailTabButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleMailTabClicked);
	}
	if (CloseWindowButton)
		CloseWindowButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestCloseWindow);
	if (!RetryLoadButton && LoadStateText)
	{
		// Independent windows no longer expose a tab bar. Recovery remains visible
		// even when an older authored navigation row is collapsed.
		UPanelWidget* Parent = LoadStateText->GetParent();
		if (Parent && Parent->CanAddMoreChildren())
		{
			RetryLoadButton = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
			Label->SetText(NSLOCTEXT("RPGItemUI", "RetryLoad", "Retry item load"));
			RetryLoadButton->SetContent(Label);
			Parent->AddChild(RetryLoadButton);
		}
	}
	if (RetryLoadButton)
	{
		RetryLoadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRetryLoadClicked);
	}
	if (!InventoryRecoveryButton && CommandResultText)
	{
		auto* Parent = CommandResultText->GetParent();
		if (Parent && Parent->CanAddMoreChildren())
		{
			InventoryRecoveryButton = WidgetTree->ConstructWidget<UButton>();
			auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
			Label->SetText(NSLOCTEXT("RPGItemUI", "RetryInventoryCommand", "Recover pending item command"));
			InventoryRecoveryButton->SetContent(Label);
			Parent->AddChild(InventoryRecoveryButton);
		}
	}
	if (InventoryRecoveryButton)
		InventoryRecoveryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleInventoryRecoveryClicked);
	if (CommandResultText) CommandResultText->SetAutoWrapText(true);
	SetIsFocusable(true);
	EnsureBagTabControls();
	EnsureSplitQuantityControls();
	CancelSplitQuantitySelection();

	BindViewModel();
	RefreshFromViewModel();
}

FKey URPGInventoryProjectionWidget::GetWindowDragMouseButton() const
{
	return EKeys::LeftMouseButton;
}

void URPGInventoryProjectionWidget::RequestCloseWindow()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (IsActivated()) DeactivateWidget();
	else OnWindowClosed.Broadcast();
}

TOptional<FSlateRect> URPGInventoryProjectionWidget::GetWindowDragScreenBounds() const
{
	if (!GetOwningPlayer())
	{
		return {};
	}
	// Use this player's screen rather than the entire desktop/editor window.
	const FGeometry ScreenGeometry =
		UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(GetOwningPlayer());
	const FSlateRect Bounds = ScreenGeometry.GetRenderBoundingRect();
	return Bounds.IsValid() && !Bounds.IsEmpty()
		? TOptional<FSlateRect>(Bounds) : TOptional<FSlateRect>();
}

void URPGInventoryProjectionWidget::NativeDestruct()
{
	if (CloseWindowButton) CloseWindowButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCloseWindow);
	CancelSplitQuantitySelection();
	if (SplitQuantityPanel) SplitQuantityPanel->RemoveFromParent();
	if (SplitQuantityInput)
	{
		SplitQuantityInput->OnTextChanged.RemoveDynamic(this, &ThisClass::HandleSplitQuantityChanged);
		SplitQuantityInput->OnTextCommitted.RemoveDynamic(this, &ThisClass::HandleSplitQuantityCommitted);
	}
	if (ConfirmSplitQuantityButton) ConfirmSplitQuantityButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConfirmSplitQuantity);
	if (CancelSplitQuantityButton) CancelSplitQuantityButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCancelSplitQuantity);
	SplitQuantityPanel = nullptr; SplitQuantityDescription = nullptr; SplitQuantityInput = nullptr;
	ConfirmSplitQuantityButton = nullptr; CancelSplitQuantityButton = nullptr;
	OnWindowClosed.Broadcast();
	if (MailTabButton)
	{
		MailTabButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleMailTabClicked);
		MailTabButton->RemoveFromParent();
		MailTabButton = nullptr;
	}
	if (RetryLoadButton)
	{
		RetryLoadButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRetryLoadClicked);
		RetryLoadButton->RemoveFromParent();
		RetryLoadButton = nullptr;
	}
	if (InventoryRecoveryButton)
	{
		InventoryRecoveryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleInventoryRecoveryClicked);
		InventoryRecoveryButton->RemoveFromParent();
		InventoryRecoveryButton = nullptr;
	}
	if (InventoryTabButton)
	{
		InventoryTabButton->OnClicked.RemoveDynamic(
			this, &ThisClass::HandleInventoryTabClicked);
	}
	if (EquipmentTabButton)
	{
		EquipmentTabButton->OnClicked.RemoveDynamic(
			this, &ThisClass::HandleEquipmentTabClicked);
	}
	UnbindViewModel();
	if (bOwnsInventoryViewModel && InventoryViewModel)
	{
		InventoryViewModel->InitializeFromPlayerController(nullptr);
	}
	InventoryViewModel = nullptr;
	bOwnsInventoryViewModel = false;

	Super::NativeDestruct();
}

void URPGInventoryProjectionWidget::NativeOnDeactivated()
{
	CancelSplitQuantitySelection();
	Super::NativeOnDeactivated();
	OnWindowClosed.Broadcast();
}

FReply URPGInventoryProjectionWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (bSplitQuantityOpen && (Event.GetKey() == EKeys::Escape || RPGConfirmationInput::IsBack(Event)))
	{
		HandleCancelSplitQuantity();
		return FReply::Handled();
	}
	if (ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		if (Controller->HandlePlayerWindowKeyDown(this, Event)) return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void URPGInventoryProjectionWidget::RefreshFromViewModel()
{
	if (!InventoryViewModel)
	{
		return;
	}
	RefreshSplitQuantitySelection();
	RefreshStatusFromViewModel();
	if (InventoryItemsText)
		InventoryItemsText->SetText(BuildProjectionText(InventoryViewModel->InventoryItems.FilterByPredicate(
			[this](const auto& Entry) { return RPGItemInventoryPolicy::Contains(ActiveCategory, Entry.GetSlotIndex()); }), false));
	if (EquipmentItemsText)
		EquipmentItemsText->SetText(BuildProjectionText(InventoryViewModel->EquipmentItems, true));
	if (InventoryGrid)
		FillInventoryGrid(WidgetTree, InventoryGrid, InventoryViewModel->InventoryItems, InventoryViewModel, ActiveCategory);
	if (EquipmentLeftSlots && EquipmentRightSlots)
		FillEquipmentSlots(WidgetTree, EquipmentLeftSlots, EquipmentRightSlots, InventoryViewModel->EquipmentItems, InventoryViewModel);
}

void URPGInventoryProjectionWidget::RefreshStatusFromViewModel()
{
	if (!InventoryViewModel) return;
	if (SortBagButton) SortBagButton->SetIsEnabled(InventoryViewModel->CanSortInventoryTab(ActiveCategory));
	if (BagEquipmentButton) BagEquipmentButton->SetIsEnabled(ActiveCategory != EItemCategory::Equip);
	if (BagConsumablesButton) BagConsumablesButton->SetIsEnabled(ActiveCategory != EItemCategory::Consume);
	if (BagOtherButton) BagOtherButton->SetIsEnabled(ActiveCategory != EItemCategory::Craft);
	RefreshInventoryCapacity();
	FString StateMessage = FString::Printf(
		TEXT("Projection: %s"),
		*GetEnumDisplayName(
			StaticEnum<ERPGInventoryProjectionLoadState>(),
			static_cast<int64>(InventoryViewModel->LoadState)));
	if (!InventoryViewModel->RewardDeliveryMessage.IsEmpty())
	{
		StateMessage += TEXT("\n") + InventoryViewModel->RewardDeliveryMessage.ToString();
	}
	if (InventoryViewModel->bHasInventoryRecovery)
		StateMessage += TEXT("\n") + NSLOCTEXT("RPGItemUI", "InventoryUncertain", "Item save result unknown. Recover the original command before changing items.").ToString();
	if (InventoryViewModel->bHasInventoryRecovery && !InventoryViewModel->bCanRetryInventoryRecovery)
		StateMessage += TEXT("\n") + (InventoryViewModel->bInventoryRecoveryInFlight
			? NSLOCTEXT("RPGItemUI", "InventoryRecoveryInFlight", "Checking the original item command...")
			: NSLOCTEXT("RPGItemUI", "InventoryRecoveryCooldown", "Please wait before retrying recovery.")).ToString();
	LoadStateText->SetText(FText::FromString(StateMessage));
	if (InventoryViewModel->bInventoryRecoveryStorageBlocked)
		LoadStateText->SetText(NSLOCTEXT("RPGItemUI", "InventoryRecoveryStorageBlocked",
			"Item recovery storage is unavailable. Item changes are blocked; stored commands have not been discarded."));
	if (InventoryViewModel->bInventoryAuthorityRevoked)
		LoadStateText->SetText(NSLOCTEXT("RPGItemUI", "InventoryAuthorityRevoked",
			"Item access for this dungeon has ended. Stored changes have not been rolled back."));
	if (InventoryRecoveryButton)
	{
		InventoryRecoveryButton->SetVisibility(InventoryViewModel->bHasInventoryRecovery ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		InventoryRecoveryButton->SetIsEnabled(InventoryViewModel->bCanRetryInventoryRecovery);
	}
	if (RetryLoadButton)
	{
		RetryLoadButton->SetVisibility(InventoryViewModel->LoadState ==
			ERPGInventoryProjectionLoadState::Failed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CommandResultText)
		CommandResultText->SetText(InventoryViewModel->GetCommandFeedbackText());
}

void URPGInventoryProjectionWidget::RefreshInteractionFromViewModel()
{
	if (SortBagButton && InventoryViewModel) SortBagButton->SetIsEnabled(InventoryViewModel->CanSortInventoryTab(ActiveCategory));
	if (!InventoryViewModel) return;
	RefreshSplitQuantitySelection();
	RefreshStatusFromViewModel();
	if (WidgetTree) WidgetTree->ForEachWidget([](UWidget* Widget)
	{
		if (auto* Cell = Cast<URPGInventoryProjectionItemSlotWidget>(Widget)) Cell->RefreshInteractionState();
	});
}

void URPGInventoryProjectionWidget::RefreshInventoryCapacity()
{
	if (!InventoryCapacityText || !InventoryViewModel) return;
	const FRPGInventoryCapacityPresentation Capacity = InventoryViewModel->GetInventoryCapacityForCategory(ActiveCategory);
	const FLinearColor Muted(0.65f, 0.72f, 0.79f, 1.f);
	if (!Capacity.bHasSnapshot)
	{
		InventoryCapacityText->SetText(NSLOCTEXT("RPGItemUI", "CapacityUnavailable", "Bag capacity unavailable."));
		InventoryCapacityText->SetColorAndOpacity(FSlateColor(Muted));
		return;
	}
	TArray<FText> Parts;
	Parts.Add(FText::Format(NSLOCTEXT("RPGItemUI", "CapacityCounts", "Bag: {0}/{1} | Free: {2}"),
		FText::AsNumber(Capacity.OccupiedSlots), FText::AsNumber(Capacity.TotalSlots), FText::AsNumber(Capacity.FreeSlots)));
	if (Capacity.ReservedEmptySlots > 0)
		Parts.Add(FText::Format(NSLOCTEXT("RPGItemUI", "CapacityReserved", "Reserved: {0}"),
			FText::AsNumber(Capacity.ReservedEmptySlots)));
	FLinearColor Color(0.75f, 0.83f, 0.71f, 1.f);
	if (Capacity.bItemChangesBlocked)
	{
		Parts.Add(NSLOCTEXT("RPGItemUI", "CapacityChangesBlocked", "Item changes unavailable"));
		Color = Muted;
	}
	else if (Capacity.OccupiedSlots == Capacity.TotalSlots)
	{
		Parts.Add(NSLOCTEXT("RPGItemUI", "CapacityFull", "Bag full"));
		Color = FLinearColor(1.f, 0.45f, 0.35f, 1.f);
	}
	else if (Capacity.FreeSlots == 0)
	{
		Parts.Add(NSLOCTEXT("RPGItemUI", "CapacityAllReserved", "All empty slots reserved"));
		Color = FLinearColor(1.f, 0.78f, 0.35f, 1.f);
	}
	InventoryCapacityText->SetText(FText::Join(FText::FromString(TEXT(" | ")), Parts));
	InventoryCapacityText->SetColorAndOpacity(FSlateColor(Color));
}

FText URPGInventoryProjectionWidget::GetDisplayedStatus() const
{
	return LoadStateText ? LoadStateText->GetText() : FText::GetEmpty();
}

void URPGInventoryProjectionWidget::BindViewModel()
{
	if (!InventoryViewModel || bViewModelBound)
	{
		return;
	}

	const INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
			this,
			&ThisClass::HandleViewModelFieldChanged);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::
			LoadState,
		Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::
			InventoryItems,
		Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::
			EquipmentItems,
		Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::MailItems,
		Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::RewardDeliveryMessage,
		Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::
			LastCommandResult,
		Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::
			bHasLastCommandResult,
		Delegate);
	bViewModelBound = true;
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::bHasInventoryRecovery, Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::bCanRetryInventoryRecovery, Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::bInventoryRecoveryInFlight, Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::bInventoryAuthorityRevoked, Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::bInventoryRecoveryStorageBlocked, Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::PendingItemCommandCount, Delegate);
	InventoryViewModel->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::LocalActionFeedback, Delegate);
}

void URPGInventoryProjectionWidget::UnbindViewModel()
{
	if (InventoryViewModel && bViewModelBound)
	{
		InventoryViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	bViewModelBound = false;
}

void URPGInventoryProjectionWidget::HandleViewModelFieldChanged(
	UObject* Object,
	UE::FieldNotification::FFieldId FieldId)
{
	if (Object == InventoryViewModel)
	{
		using FFields = URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor;
		if (FieldId == FFields::InventoryItems || FieldId == FFields::EquipmentItems || FieldId == FFields::MailItems)
			RefreshFromViewModel();
		else
			RefreshInteractionFromViewModel();
	}
}

void URPGInventoryProjectionWidget::HandleInventoryTabClicked()
{
	if (ARPGPlayerController* Controller =
		Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		Controller->ShowInventoryProjection();
	}
}

void URPGInventoryProjectionWidget::HandleEquipmentTabClicked()
{
	if (ARPGPlayerController* Controller =
		Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		Controller->ShowEquipmentProjection();
	}
}

void URPGInventoryProjectionWidget::HandleMailTabClicked()
{
	if (ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		Controller->ShowRewardMail();
	}
}

void URPGInventoryProjectionWidget::HandleRetryLoadClicked()
{
	if (InventoryViewModel) InventoryViewModel->RequestRetryLoad();
}

void URPGInventoryProjectionWidget::HandleInventoryRecoveryClicked()
{
	if (InventoryViewModel) InventoryViewModel->RequestRetryInventoryCommand();
}

bool URPGInventoryProjectionWidget::EnsureSplitQuantityControls()
{
	if (SplitQuantityPanel) return true;
	if (!WidgetTree || !InventoryGrid) return false; // not the separate equipment window
	auto* Parent = CommandResultText ? Cast<UVerticalBox>(CommandResultText->GetParent()) : nullptr;
	if (!Parent && LoadStateText) Parent = Cast<UVerticalBox>(LoadStateText->GetParent());
	if (!Parent) return false; // unsupported BP layouts fail closed, without a whole-stack move
	SplitQuantityPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SplitQuantityPanel"));
	Parent->AddChildToVerticalBox(SplitQuantityPanel);
	SplitQuantityPanel->SetVisibility(ESlateVisibility::Collapsed);
	SplitQuantityDescription = WidgetTree->ConstructWidget<UTextBlock>();
	SplitQuantityDescription->SetAutoWrapText(true);
	SplitQuantityPanel->AddChildToVerticalBox(SplitQuantityDescription);
	auto* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	SplitQuantityPanel->AddChildToVerticalBox(Actions);
	SplitQuantityInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SplitQuantityInput"));
	SplitQuantityInput->SetMinDesiredWidth(120.f);
	SplitQuantityInput->SetSelectAllTextWhenFocused(true);
	SplitQuantityInput->SetClearKeyboardFocusOnCommit(false);
	SplitQuantityInput->SetHintText(NSLOCTEXT("RPGItemUI", "SplitWholeQuantity", "Whole quantity"));
	Actions->AddChildToHorizontalBox(SplitQuantityInput);
	const auto AddButton = [this, Actions](const FName Name, const FText& Label)
	{
		auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(Label); Button->SetContent(Text); Actions->AddChildToHorizontalBox(Button);
		return Button;
	};
	ConfirmSplitQuantityButton = AddButton(TEXT("ConfirmSplitQuantityButton"), NSLOCTEXT("RPGItemUI", "ConfirmSplit", "Split"));
	CancelSplitQuantityButton = AddButton(TEXT("CancelSplitQuantityButton"), NSLOCTEXT("RPGItemUI", "CancelSplit", "Cancel"));
	SplitQuantityInput->OnTextChanged.AddUniqueDynamic(this, &ThisClass::HandleSplitQuantityChanged);
	SplitQuantityInput->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::HandleSplitQuantityCommitted);
	ConfirmSplitQuantityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConfirmSplitQuantity);
	CancelSplitQuantityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCancelSplitQuantity);
	return true;
}

bool URPGInventoryProjectionWidget::OpenSplitQuantitySelection(const FGuid ItemId,
	const int64 ExpectedRevision, const int32 DestinationSlot)
{
	FRPGInventoryProjectionEntry Source;
	if (bSplitQuantityOpen || !InventoryViewModel || !EnsureSplitQuantityControls() ||
		!InventoryViewModel->CanSplitItem(ItemId, ExpectedRevision, DestinationSlot, 1) ||
		!InventoryViewModel->TryGetInventoryDragEntry(ItemId, Source)) return false;
	SplitSourceId = ItemId; SplitExpectedRevision = ExpectedRevision;
	SplitDestinationSlot = DestinationSlot; SplitSourceQuantity = Source.GetQuantity();
	bSplitQuantityOpen = true;
	SplitQuantityDescription->SetText(FText::Format(NSLOCTEXT("RPGItemUI", "SplitQuantityRange",
		"Split {0} to bag slot {1}: enter 1 to {2}. Enter confirms; Esc cancels."),
		InventoryViewModel->GetItemPresentation(ItemId).DisplayName, FText::AsNumber(DestinationSlot),
		FText::AsNumber(SplitSourceQuantity - 1)));
	SplitQuantityInput->SetText(FText::FromString(FString::FromInt(SplitSourceQuantity / 2)));
	SplitQuantityPanel->SetVisibility(ESlateVisibility::Visible);
	RefreshSplitQuantitySelection();
	if (IsInViewport() && GetOwningPlayer()) SplitQuantityInput->SetUserFocus(GetOwningPlayer());
	return bSplitQuantityOpen;
}

bool URPGInventoryProjectionWidget::IsSplitSnapshotValid() const
{
	FRPGInventoryProjectionEntry Source;
	return bSplitQuantityOpen && InventoryViewModel &&
		InventoryViewModel->CanSplitItem(SplitSourceId, SplitExpectedRevision, SplitDestinationSlot, 1) &&
		InventoryViewModel->TryGetInventoryDragEntry(SplitSourceId, Source) && Source.GetQuantity() == SplitSourceQuantity;
}

bool URPGInventoryProjectionWidget::TryReadSplitQuantity(int32& OutQuantity) const
{
	OutQuantity = 0;
	if (!SplitQuantityInput || SplitSourceQuantity < 2) return false;
	const FString Text = SplitQuantityInput->GetText().ToString();
	if (Text.IsEmpty() || Text.Len() > 10) return false;
	int64 Value = 0;
	for (const TCHAR Digit : Text)
	{
		if (Digit < TCHAR('0') || Digit > TCHAR('9')) return false;
		Value = Value * 10 + (Digit - TCHAR('0'));
		if (Value >= SplitSourceQuantity) return false;
	}
	if (Value <= 0) return false;
	OutQuantity = static_cast<int32>(Value);
	return true;
}

bool URPGInventoryProjectionWidget::CanConfirmSplitQuantitySelection() const
{
	int32 Quantity;
	return IsSplitSnapshotValid() && TryReadSplitQuantity(Quantity);
}

bool URPGInventoryProjectionWidget::ConfirmSplitQuantitySelection(FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	int32 Quantity;
	if (!IsSplitSnapshotValid()) { CancelSplitQuantitySelection(); return false; }
	if (!TryReadSplitQuantity(Quantity)) return false;
	const FGuid ItemId = SplitSourceId;
	const int64 Revision = SplitExpectedRevision;
	const int32 DestinationSlot = SplitDestinationSlot;
	CancelSplitQuantitySelection(); // close first so reentrant delegates/double clicks cannot resubmit
	return InventoryViewModel->RequestSplitItem(ItemId, Revision, DestinationSlot, Quantity, OutRequestId);
}

void URPGInventoryProjectionWidget::CancelSplitQuantitySelection()
{
	bSplitQuantityOpen = false; SplitSourceId.Invalidate(); SplitExpectedRevision = 0;
	SplitDestinationSlot = INDEX_NONE; SplitSourceQuantity = 0;
	if (SplitQuantityPanel) SplitQuantityPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (ConfirmSplitQuantityButton) ConfirmSplitQuantityButton->SetIsEnabled(false);
}

void URPGInventoryProjectionWidget::RefreshSplitQuantitySelection()
{
	if (!bSplitQuantityOpen) return;
	if (!IsSplitSnapshotValid()) { CancelSplitQuantitySelection(); return; }
	if (ConfirmSplitQuantityButton) ConfirmSplitQuantityButton->SetIsEnabled(CanConfirmSplitQuantitySelection());
}

void URPGInventoryProjectionWidget::HandleSplitQuantityChanged(const FText&)
{
	RefreshSplitQuantitySelection();
}

void URPGInventoryProjectionWidget::HandleSplitQuantityCommitted(const FText&, const ETextCommit::Type Method)
{
	if (Method == ETextCommit::OnEnter) HandleConfirmSplitQuantity();
}

void URPGInventoryProjectionWidget::HandleConfirmSplitQuantity()
{
	FGuid RequestId;
	ConfirmSplitQuantitySelection(RequestId);
	if (!bSplitQuantityOpen && IsInViewport() && GetOwningPlayer()) SetUserFocus(GetOwningPlayer());
}

void URPGInventoryProjectionWidget::HandleCancelSplitQuantity()
{
	CancelSplitQuantitySelection();
	if (IsInViewport() && GetOwningPlayer()) SetUserFocus(GetOwningPlayer());
}

void URPGInventoryProjectionWidget::EnsureBagTabControls()
{
	if (!WidgetTree || !InventoryGrid || BagEquipmentButton) return;
	auto* Parent = LoadStateText ? LoadStateText->GetParent() : nullptr;
	if (!Parent || !Parent->CanAddMoreChildren()) return;
	auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BagCategoryControls"));
	Parent->AddChild(Row);
	const auto Add = [this, Row](FName Name, const FText& Label)
	{
		auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(Label); Button->SetContent(Text); Row->AddChildToHorizontalBox(Button);
		return Button;
	};
	BagEquipmentButton = Add(TEXT("BagEquipmentButton"), NSLOCTEXT("RPGItemUI", "BagEquipment80", "장비 (80)"));
	BagConsumablesButton = Add(TEXT("BagConsumablesButton"), NSLOCTEXT("RPGItemUI", "BagConsumables80", "소비 (80)"));
	BagOtherButton = Add(TEXT("BagOtherButton"), NSLOCTEXT("RPGItemUI", "BagOther80", "기타 (80)"));
	SortBagButton = Add(TEXT("SortBagButton"), NSLOCTEXT("RPGItemUI", "SortBagTab", "정렬"));
	BagEquipmentButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBagEquipment);
	BagConsumablesButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBagConsumables);
	BagOtherButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBagOther);
	SortBagButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSortBag);
}
void URPGInventoryProjectionWidget::SelectInventoryCategory(EItemCategory Category)
{
	if (!RPGItemInventoryPolicy::IsTab(Category) || Category == ActiveCategory) return;
	CancelSplitQuantitySelection();
	ActiveCategory = Category;
	RefreshFromViewModel();
}
void URPGInventoryProjectionWidget::HandleBagEquipment() { SelectInventoryCategory(EItemCategory::Equip); }
void URPGInventoryProjectionWidget::HandleBagConsumables() { SelectInventoryCategory(EItemCategory::Consume); }
void URPGInventoryProjectionWidget::HandleBagOther() { SelectInventoryCategory(EItemCategory::Craft); }
void URPGInventoryProjectionWidget::HandleSortBag()
{
	FGuid Request;
	if (InventoryViewModel) InventoryViewModel->RequestSortInventoryTab(ActiveCategory, Request);
}
