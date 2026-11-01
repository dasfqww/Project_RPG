#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "UI/Inventory/Projection/RPGItemIdDragDropOperation.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGInventoryProjectionItemSlotWidget)

void URPGInventoryProjectionItemSlotWidget::SetItem(
	const FGuid InItemId, const FText& InLabel,
	URPGInventoryProjectionViewModel* InViewModel, const bool bInEquipment,
	const int32 InContainerSlotIndex)
{
	ItemId = InItemId;
	ViewModel = InViewModel;
	bEquipment = bInEquipment;
	InventorySlotIndex = bEquipment ? INDEX_NONE : InContainerSlotIndex;
	EquipmentSlotIndex = bEquipment ? InContainerSlotIndex : INDEX_NONE;
	// Empty cells must remain hit-testable across their entire allocated area.
	SetVisibility(ESlateVisibility::Visible);
	UpdateFrameColor();
	const FText Hint = bEquipment
		? NSLOCTEXT("RPGItemUI", "UnequipHint", "Right-click: unequip into a free inventory slot")
		: NSLOCTEXT("RPGItemUI", "UseEquipSelectSplitMoveHint", "Right-click: use or equip\nDrag: move, merge, swap, or assign to quick slot\nShift + drag to an empty bag slot: split half\nCtrl + drag to an empty bag slot: choose split quantity");
	const auto Presentation = InViewModel
		? InViewModel->GetItemPresentation(ItemId) : URPGInventoryProjectionViewModel::FItemPresentation();
	const FText Details = Presentation.GetDetailsText();
	BaseToolTip = !ItemId.IsValid() ? FText::GetEmpty() : (Details.IsEmpty() ? Hint
		: FText::Format(NSLOCTEXT("RPGItemUI", "ItemDetailsAndHint", "{0}\n\n{1}"), Details, Hint));
	// Empty equipment still identifies its slot; empty bag cells must clear old names.
	FallbackLabel = ItemId.IsValid()
		? (Presentation.DisplayName.IsEmpty() ? InLabel : Presentation.DisplayName)
		: (bEquipment ? InLabel : FText::GetEmpty());
	IconTexture = Presentation.Icon;
	DisplayedQuantity = Presentation.Quantity;
	RefreshItemVisuals();
}

bool URPGInventoryProjectionItemSlotWidget::RequestDefaultAction()
{
	URPGInventoryProjectionViewModel* Model = ViewModel.Get();
	if (!ItemId.IsValid() || !Model || GetCurrentPresentationState() != ERPGItemSlotPresentationState::Idle)
		return false;
	FGuid RequestId;
	return bEquipment
		? Model->RequestUnequipToFirstFreeSlot(ItemId, RequestId)
		: Model->RequestDefaultAction(ItemId, RequestId);
}

void URPGInventoryProjectionItemSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	SetVisibility(ESlateVisibility::Visible);
	Frame = WidgetTree->ConstructWidget<UBorder>();
	UpdateFrameColor();
	Frame->SetPadding(FMargin(3.f));
	Frame->SetHorizontalAlignment(HAlign_Fill);
	Frame->SetVerticalAlignment(VAlign_Fill);
	Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	USizeBox* IconBounds = WidgetTree->ConstructWidget<USizeBox>();
	IconBounds->SetWidthOverride(42.f);
	IconBounds->SetHeightOverride(42.f);
	IconBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UScaleBox* IconScale = WidgetTree->ConstructWidget<UScaleBox>();
	IconScale->SetStretch(EStretch::ScaleToFit);
	IconScale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ItemIcon"));
	IconImage->SetVisibility(ESlateVisibility::Collapsed);
	IconScale->SetContent(IconImage);
	IconBounds->SetContent(IconScale);
	UOverlaySlot* IconSlot = Layers->AddChildToOverlay(IconBounds);
	IconSlot->SetHorizontalAlignment(HAlign_Center);
	IconSlot->SetVerticalAlignment(VAlign_Center);

	LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ItemFallbackName"));
	LabelText->SetJustification(ETextJustify::Center);
	LabelText->SetAutoWrapText(true);
	LabelText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.82f, 0.47f)));
	FSlateFontInfo Font = LabelText->GetFont();
	Font.Size = 10;
	LabelText->SetFont(Font);
	LabelText->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* LabelSlot = Layers->AddChildToOverlay(LabelText);
	LabelSlot->SetHorizontalAlignment(HAlign_Fill);
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	QuantityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ItemQuantityText"));
	QuantityText->SetJustification(ETextJustify::Right);
	QuantityText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	QuantityText->SetShadowColorAndOpacity(FLinearColor::Black);
	QuantityText->SetShadowOffset(FVector2D(1.f, 1.f));
	Font = QuantityText->GetFont();
	Font.Size = 11;
	QuantityText->SetFont(Font);
	QuantityText->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* QuantitySlot = Layers->AddChildToOverlay(QuantityText);
	QuantitySlot->SetHorizontalAlignment(HAlign_Right);
	QuantitySlot->SetVerticalAlignment(VAlign_Bottom);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ItemStatusText"));
	Font = StatusText->GetFont();
	Font.Size = 8;
	StatusText->SetFont(Font);
	StatusText->SetShadowColorAndOpacity(FLinearColor::Black);
	StatusText->SetShadowOffset(FVector2D(1.f, 1.f));
	StatusText->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* StatusSlot = Layers->AddChildToOverlay(StatusText);
	StatusSlot->SetHorizontalAlignment(HAlign_Left);
	StatusSlot->SetVerticalAlignment(VAlign_Top);

	Frame->SetContent(Layers);
	WidgetTree->RootWidget = Frame;
	RefreshItemVisuals();
}

void URPGInventoryProjectionItemSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// A reattached cell must restart a load cancelled when its old Slate view closed.
	if (!IconLoadHandle.IsValid() && (!IconImage || !IconImage->GetBrush().GetResourceObject()))
	{
		RefreshItemVisuals();
	}
	RefreshInteractionState();
}

void URPGInventoryProjectionItemSlotWidget::NativeDestruct()
{
	CancelIconLoad();
	ApplyIcon(nullptr);
	Super::NativeDestruct();
}

void URPGInventoryProjectionItemSlotWidget::BeginDestroy()
{
	CancelIconLoad();
	Super::BeginDestroy();
}

void URPGInventoryProjectionItemSlotWidget::CancelIconLoad()
{
	++IconLoadGeneration;
	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}
}

void URPGInventoryProjectionItemSlotWidget::ApplyIcon(UTexture2D* Texture)
{
	if (IconImage)
	{
		IconImage->SetBrushFromTexture(Texture, true);
		IconImage->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (LabelText)
	{
		LabelText->SetVisibility((ItemId.IsValid() || bEquipment) && !Texture
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void URPGInventoryProjectionItemSlotWidget::RefreshItemVisuals()
{
	CancelIconLoad();
	if (LabelText) LabelText->SetText(FallbackLabel);
	if (QuantityText)
	{
		QuantityText->SetText(DisplayedQuantity > 1
			? FText::Format(NSLOCTEXT("RPGItemUI", "ItemQuantityBadge", "x{0}"), FText::AsNumber(DisplayedQuantity))
			: FText::GetEmpty());
		QuantityText->SetVisibility(ItemId.IsValid() && DisplayedQuantity > 1
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	ApplyIcon(nullptr);
	RefreshInteractionState();
	if (!IconImage || !ItemId.IsValid() || IconTexture.IsNull()) return;
	if (UTexture2D* Loaded = IconTexture.Get())
	{
		ApplyIcon(Loaded);
		return;
	}

	const uint64 RequestedGeneration = IconLoadGeneration;
	const TSoftObjectPtr<UTexture2D> RequestedIcon = IconTexture;
	const TWeakObjectPtr<URPGInventoryProjectionItemSlotWidget> WeakThis(this);
	IconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestedIcon.ToSoftObjectPath(),
		FStreamableDelegate::CreateLambda([WeakThis, RequestedGeneration, RequestedIcon]()
		{
			if (URPGInventoryProjectionItemSlotWidget* Self = WeakThis.Get();
				Self && Self->IconLoadGeneration == RequestedGeneration)
			{
				// A failed load leaves the name visible; a reused/closed cell ignores stale callbacks.
				Self->ApplyIcon(RequestedIcon.Get());
			}
		}));
}

FReply URPGInventoryProjectionItemSlotWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (ItemId.IsValid() && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		RequestDefaultAction();
		return FReply::Handled();
	}
	if (!bEquipment && ItemId.IsValid() && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (GetCurrentPresentationState() != ERPGItemSlotPresentationState::Idle) return FReply::Handled();
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void URPGInventoryProjectionItemSlotWidget::NativeOnDragDetected(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	URPGInventoryProjectionViewModel* Model = ViewModel.Get();
	FRPGInventoryProjectionEntry Entry;
	if (bEquipment || !Model || GetCurrentPresentationState() != ERPGItemSlotPresentationState::Idle ||
		!Model->TryGetInventoryDragEntry(ItemId, Entry))
	{
		return;
	}
	if ((InMouseEvent.IsShiftDown() || InMouseEvent.IsControlDown()) && Entry.GetQuantity() < 2) return;

	URPGItemIdDragDropOperation* Operation =
		NewObject<URPGItemIdDragDropOperation>(this);
	Operation->ItemId = ItemId;
	Operation->SourceViewModel = Model;
	Operation->ExpectedRevision = Entry.GetRevision();
	Operation->bSplitStack = InMouseEvent.IsShiftDown() || InMouseEvent.IsControlDown();
	Operation->bChooseSplitQuantity = InMouseEvent.IsControlDown();
	Operation->SplitQuantity = Operation->bSplitStack ? Entry.GetQuantity() / 2 : 0;
	const FText DragName = LabelText ? LabelText->GetText() : Model->GetItemPresentation(ItemId).DisplayName;
	const FText DragLabelText = Entry.GetQuantity() > 1
		? FText::Format(NSLOCTEXT("RPGItemUI", "ItemDragNameAndQuantity", "{0}\nx{1}"),
			DragName, FText::AsNumber(Entry.GetQuantity())) : DragName;
	UTextBlock* DragLabel = NewObject<UTextBlock>(Operation);
	DragLabel->SetText(Operation->bChooseSplitQuantity
		? NSLOCTEXT("RPGItemUI", "ChooseSplitDrag", "Choose split quantity") : Operation->bSplitStack
		? FText::Format(NSLOCTEXT("RPGItemUI", "SplitDragQuantity", "Split x{0}"), FText::AsNumber(Operation->SplitQuantity))
		: DragLabelText);
	DragLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.82f, 0.47f)));
	DragLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
	Operation->DefaultDragVisual = DragLabel;
	OutOperation = Operation;
}

bool URPGInventoryProjectionItemSlotWidget::CanAcceptInventoryDrop(
	const UDragDropOperation* InOperation) const
{
	const URPGItemIdDragDropOperation* Drag = Cast<URPGItemIdDragDropOperation>(InOperation);
	const URPGInventoryProjectionViewModel* Model = ViewModel.Get();
	if (bEquipment || !Model || !Drag || Drag->SourceViewModel.Get() != Model ||
		GetCurrentPresentationState() != ERPGItemSlotPresentationState::Idle ||
		(Drag->bChooseSplitQuantity && !Drag->bSplitStack))
	{
		return false;
	}
	if (Drag->bSplitStack)
		return !ItemId.IsValid() && Model->CanSplitItem(Drag->ItemId, Drag->ExpectedRevision,
			InventorySlotIndex, Drag->SplitQuantity);
	if (!ItemId.IsValid())
	{
		return Model->CanMoveItemToSlot(Drag->ItemId, Drag->ExpectedRevision, InventorySlotIndex);
	}
	FRPGInventoryProjectionEntry Destination;
	return Model->TryGetInventoryDragEntry(ItemId, Destination) &&
		Destination.GetSlotIndex() == InventorySlotIndex &&
		Model->GetOccupiedDropAction(Drag->ItemId, Drag->ExpectedRevision, ItemId,
			Destination.GetRevision()) != ERPGInventoryOccupiedDropAction::Rejected;
}

ERPGItemSlotPresentationState URPGInventoryProjectionItemSlotWidget::GetCurrentPresentationState() const
{
	const auto* Model = ViewModel.Get();
	return Model ? Model->GetItemSlotPresentationState(ItemId,
		bEquipment ? ERPGItemContainerType::Equipment : ERPGItemContainerType::Inventory,
		bEquipment ? EquipmentSlotIndex : InventorySlotIndex)
		: ERPGItemSlotPresentationState::Unavailable;
}

void URPGInventoryProjectionItemSlotWidget::RefreshInteractionState()
{
	using EState = ERPGItemSlotPresentationState;
	const EState State = GetCurrentPresentationState();
	FText Badge;
	FText Reason;
	FLinearColor StatusColor(1.f, .8f, .3f);
	switch (State)
	{
	case EState::Locked:
		Badge = NSLOCTEXT("RPGItemUI", "SlotLockedBadge", "LOCK");
		Reason = NSLOCTEXT("RPGItemUI", "SlotLockedReason", "This item is locked. Item actions are unavailable.");
		StatusColor = FLinearColor(1.f, .55f, .5f);
		break;
	case EState::Pending:
		Badge = NSLOCTEXT("RPGItemUI", "SlotPendingBadge", "WAIT");
		Reason = NSLOCTEXT("RPGItemUI", "SlotPendingReason", "This item or slot is reserved while an item command awaits authoritative updates.");
		break;
	case EState::RecoveryRequired:
		if (ItemId.IsValid()) Badge = NSLOCTEXT("RPGItemUI", "SlotRecoveryBadge", "CHECK");
		Reason = NSLOCTEXT("RPGItemUI", "SlotRecoveryReason", "An item command result is unresolved. Use recovery in the inventory window before further item actions.");
		StatusColor = FLinearColor(1.f, .65f, .3f);
		break;
	case EState::Unavailable:
		Reason = NSLOCTEXT("RPGItemUI", "SlotUnavailableReason", "Item actions are unavailable for the current inventory state.");
		break;
	case EState::Idle:
		break;
	}
	if (StatusText)
	{
		StatusText->SetText(Badge);
		StatusText->SetColorAndOpacity(FSlateColor(StatusColor));
		StatusText->SetVisibility(Badge.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (LabelText)
	{
		LabelText->SetColorAndOpacity(FSlateColor(State == EState::Idle
			? FLinearColor(1.f, .82f, .47f) : FLinearColor(.65f, .65f, .65f)));
		if (auto* LabelOverlaySlot = Cast<UOverlaySlot>(LabelText->Slot))
			LabelOverlaySlot->SetPadding(FMargin(0.f, Badge.IsEmpty() ? 0.f : 11.f, 0.f, 12.f));
	}
	if (IconImage) IconImage->SetColorAndOpacity(State == EState::Idle
		? FLinearColor::White : FLinearColor(.5f, .5f, .5f));
	const bool bShowReason = !Reason.IsEmpty() && (ItemId.IsValid() || State == EState::Pending);
	SetToolTipText(!bShowReason ? BaseToolTip : BaseToolTip.IsEmpty() ? Reason
		: FText::Format(NSLOCTEXT("RPGItemUI", "ItemDetailsAndStatus", "{0}\n\n{1}"), BaseToolTip, Reason));
	UpdateFrameColor();
}

void URPGInventoryProjectionItemSlotWidget::UpdateFrameColor(const bool bValidDropTarget)
{
	if (Frame)
	{
		using EState = ERPGItemSlotPresentationState;
		const EState State = GetCurrentPresentationState();
		FLinearColor Color = ItemId.IsValid() ? FLinearColor(.27f, .16f, .08f) : FLinearColor(.055f, .075f, .10f);
		switch (State)
		{
		case EState::Locked: Color = FLinearColor(.16f, .065f, .065f); break;
		case EState::Pending: Color = FLinearColor(.23f, .18f, .07f); break;
		case EState::RecoveryRequired: Color = FLinearColor(.18f, .12f, .06f); break;
		case EState::Unavailable: Color = FLinearColor(.065f, .065f, .065f); break;
		case EState::Idle: if (bValidDropTarget) Color = FLinearColor(.12f, .32f, .20f); break;
		}
		Frame->SetBrushColor(Color);
	}
}

bool URPGInventoryProjectionItemSlotWidget::NativeOnDragOver(
	const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	const bool bCanAccept = CanAcceptInventoryDrop(InOperation);
	UpdateFrameColor(bCanAccept);
	return bCanAccept || Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

void URPGInventoryProjectionItemSlotWidget::NativeOnDragLeave(
	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UpdateFrameColor();
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
}

bool URPGInventoryProjectionItemSlotWidget::NativeOnDrop(
	const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	UpdateFrameColor();
	if (!CanAcceptInventoryDrop(InOperation))
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}
	const URPGItemIdDragDropOperation* Drag = Cast<URPGItemIdDragDropOperation>(InOperation);
	FGuid RequestId;
	if (Drag->bChooseSplitQuantity)
	{
		auto* Screen = GetTypedOuter<URPGInventoryProjectionWidget>();
		return Screen && Screen->GetInventoryViewModel() == ViewModel.Get() &&
			Screen->OpenSplitQuantitySelection(Drag->ItemId, Drag->ExpectedRevision, InventorySlotIndex);
	}
	if (Drag->bSplitStack)
		return ViewModel->RequestSplitItem(Drag->ItemId, Drag->ExpectedRevision,
			InventorySlotIndex, Drag->SplitQuantity, RequestId);
	if (ItemId.IsValid())
	{
		FRPGInventoryProjectionEntry Destination;
		if (!ViewModel->TryGetInventoryDragEntry(ItemId, Destination)) return false;
		const auto Action = ViewModel->GetOccupiedDropAction(Drag->ItemId, Drag->ExpectedRevision,
			ItemId, Destination.GetRevision());
		if (Action == ERPGInventoryOccupiedDropAction::Merge)
			return ViewModel->RequestMergeItems(Drag->ItemId, Drag->ExpectedRevision,
				ItemId, Destination.GetRevision(), RequestId);
		return Action == ERPGInventoryOccupiedDropAction::Swap &&
			ViewModel->RequestSwapItems(Drag->ItemId, Drag->ExpectedRevision, ItemId,
				Destination.GetRevision(), RequestId);
	}
	return ViewModel->RequestMoveItem(Drag->ItemId, Drag->ExpectedRevision,
		InventorySlotIndex, RequestId);
}
