// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/PopUp/RPGItemPopUp.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "UI/PopUp/RPGItemSplitPopUp.h"
#include "Item/RPGItemBase.h"
#include "UI/GridSlot/RPGGridSlot.h"
#include "UI/RPGInventoryItemSlot.h"
#include "UI/Inventory/Hover/RPGHoverItem.h"

void URPGItemPopUp::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SplitButton->OnClicked.AddDynamic(this, &ThisClass::PopUpItemSplitWidget);
	DropButton->OnClicked.AddDynamic(this, &ThisClass::DropItem);
	ConsumeButton->OnClicked.AddDynamic(this, &ThisClass::ConsumeItem);
}

void URPGItemPopUp::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	RemoveFromParent();
}

void URPGItemPopUp::PopUpItemSplitWidget()
{
	//create itemsplit popupwidget
	URPGItemSplitPopUp* ItemSplitPopUp = CreateWidget<URPGItemSplitPopUp>(this, ItemSplitPopUpClass);

	if (!IsValid(ItemSplitPopUp)) return;

	ItemSplitPopUp->OnSplit.BindDynamic(this, &ThisClass::OnPopUpMenuSplit);
	ItemSplitPopUp->SetGridIndex(GridIndex);
	ItemSplitPopUp->SetSliderParams(SliderMax, FMath::Max(1, (SliderMax + 1) / 2));
	ItemSplitPopUp->AddToViewport();

	RemoveFromParent();
}

void URPGItemPopUp::DropItem()
{
	if (OnDrop.ExecuteIfBound(GridIndex))
	{
		RemoveFromParent();
	}
}

void URPGItemPopUp::ConsumeItem()
{
	if (OnConsume.ExecuteIfBound(GridIndex))
	{
		RemoveFromParent();

	}
}

void URPGItemPopUp::OnPopUpMenuSplit(int32 SplitAmount, int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return;

	URPGGridSlot* GridSlot = GridSlots[Index];
	URPGItemBase* ClickedItem = GridSlot->GetInvenItem().Get();
	if (!IsValid(ClickedItem)) return;
	if (!ClickedItem->IsStackable()) return;

	TObjectPtr<URPGInventoryItemSlot>* ItemSlot = ItemsInSlot.Find(Index);
	if (!ItemSlot || !IsValid(ItemSlot->Get())) return;

	const int32 Quantity = GridSlot->GetQuantity();
	if (SplitAmount <= 0 || SplitAmount >= Quantity) return;

	const int32 NewQuantity = Quantity - SplitAmount;

	GridSlot->SetQuantity(NewQuantity);
	(*ItemSlot)->UpdateItemQuantity(NewQuantity);

	if (OnAssignHoverItem.IsBound())
	{
		OnAssignHoverItem.Execute(ClickedItem, Index, Index, this);
		if (IsValid(HoverItem))
		{
			HoverItem->UpdateQuantity(SplitAmount);
		}
	}
}

void URPGItemPopUp::CollapseSplitButton() const
{
	SplitButton->SetVisibility(ESlateVisibility::Collapsed);
}

void URPGItemPopUp::CollapseConsumeButton() const
{
	ConsumeButton->SetVisibility(ESlateVisibility::Collapsed);
}

FVector2D URPGItemPopUp::GetBoxSize() const
{
	return FVector2D(RootSizeBox->GetWidthOverride(), RootSizeBox->GetHeightOverride());
}
