// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Inventory/Spatial/RPGSpatialInventory.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "UI/Inventory/Spatial/RPGInventoryGrid.h"
#include "UI/RPGInventoryTooltip.h"
#include "RPGFunctionLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Item/RPGItemBase.h"

#include "RPGDebugHelper.h"

void URPGSpatialInventory::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	Button_Equip->OnClicked.AddDynamic(this, &ThisClass::ShowEquipInven);
	Button_Consume->OnClicked.AddDynamic(this, &ThisClass::ShowConsumeInven);
	Button_Craft->OnClicked.AddDynamic(this, &ThisClass::ShowCraftInven);

	Grid_Equip->SetOwningCanvas(CanvasPanel);
	Grid_Consume->SetOwningCanvas(CanvasPanel);
	Grid_Craft->SetOwningCanvas(CanvasPanel);

	ShowEquipInven();
}

FReply URPGSpatialInventory::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	ActiveGrid->DropItem();

	FReply ParentReply = Super::NativeOnMouseButtonDown(MyGeometry, MouseEvent);

	if (ParentReply.IsEventHandled())
	{
		return ParentReply;
	}

	return FReply::Handled();
}

void URPGSpatialInventory::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!IsValid(ItemToolTip)) return;
	SetItemTooltipSizeAndPosition(ItemToolTip, CanvasPanel);
}

FSlotAvailabilityResult URPGSpatialInventory::HasSpaceForItem(ARPGPickUpBase* ItemPickup) const
{
	switch (URPGFunctionLibrary::GetItemCategoryFromItemPickup(ItemPickup))
	{
		case EItemCategory::Equip:
			return Grid_Equip->HasSpaceForItem(ItemPickup);
		case EItemCategory::Consume:
			return Grid_Consume->HasSpaceForItem(ItemPickup);
		case EItemCategory::Craft:
			return Grid_Craft->HasSpaceForItem(ItemPickup);
		default:
			Debug::Print("ItemComponent doesn't have a valid Item Category.");
			return FSlotAvailabilityResult();
	}
}

void URPGSpatialInventory::OnItemHovered(URPGItemBase* Item)
{
	const auto& Manifest = Item->GetItemManifest();
	URPGInventoryTooltip* TooltipWidget = GetItemTooltip();
	TooltipWidget->SetVisibility(ESlateVisibility::Collapsed);

	GetOwningPlayer()->GetWorldTimerManager().ClearTimer(TooltipTimer);

	FTimerDelegate ToolTipTimerDelegate;
	ToolTipTimerDelegate.BindLambda([this, &Manifest, TooltipWidget]()
		{
			Manifest.AssimilateInventoryFragments(TooltipWidget);
			GetItemTooltip()->SetVisibility(ESlateVisibility::HitTestInvisible);
		});

	GetOwningPlayer()->GetWorldTimerManager().SetTimer(TooltipTimer,ToolTipTimerDelegate, TooltipTimerDelay, false);
}

void URPGSpatialInventory::OnItemUnHovered()
{
	GetItemTooltip()->SetVisibility(ESlateVisibility::Collapsed);
	//GetOwningPlayer()->GetWorldTimerManager().ClearTimer(TooltipTimer);
}

bool URPGSpatialInventory::HasHoverItem() const
{
	if (Grid_Equip->HasHoverItem()) return true;
	if (Grid_Consume->HasHoverItem()) return true;
	if (Grid_Craft->HasHoverItem()) return true;
	return false;
}

URPGHoverItem* URPGSpatialInventory::GetHoverItem() const
{
	if (!ActiveGrid.IsValid()) return nullptr;
	return ActiveGrid->GetHoverItem();
}

void URPGSpatialInventory::ShowEquipInven()
{
	SetActiveGrid(Grid_Equip, Button_Equip);
}

void URPGSpatialInventory::ShowConsumeInven()
{
	SetActiveGrid(Grid_Consume, Button_Consume);
}

void URPGSpatialInventory::ShowCraftInven()
{
	SetActiveGrid(Grid_Craft, Button_Craft);
}

void URPGSpatialInventory::DisableButton(UButton* Button)
{
	Button_Equip->SetIsEnabled(true);
	Button_Consume->SetIsEnabled(true);
	Button_Craft->SetIsEnabled(true);
	Button->SetIsEnabled(false);
}

void URPGSpatialInventory::SetActiveGrid(URPGInventoryGrid* Grid, UButton* Button)
{
	
	ActiveGrid = Grid;
	if (ActiveGrid.IsValid()) ActiveGrid->SetVisibleCursor();

	DisableButton(Button);
	Switcher->SetActiveWidget(Grid);
}

void URPGSpatialInventory::SetItemTooltipSizeAndPosition(URPGInventoryTooltip* Description,
	UCanvasPanel* Canvas) const
{
	UCanvasPanelSlot* ItemTooltipCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(Description);
	if (!IsValid(ItemTooltipCPS)) return;

	const FVector2D ItemToolTipSize = ItemToolTip->GetBoxSize();
	ItemTooltipCPS->SetSize(ItemToolTipSize);

	FVector2D ClampedPosition = URPGFunctionLibrary::GetClampedWidgetPosition(
		URPGFunctionLibrary::GetWidgetSize(Canvas),
		ItemToolTipSize,
		UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer()));

	ItemTooltipCPS->SetPosition(ClampedPosition);
}

URPGInventoryTooltip* URPGSpatialInventory::GetItemTooltip()
{
	if (!IsValid(ItemToolTip))
	{
		ItemToolTip = CreateWidget<URPGInventoryTooltip>(GetOwningPlayer(), ItemToolTipClass);
		CanvasPanel->AddChild(ItemToolTip);
	}

	return ItemToolTip;
}
