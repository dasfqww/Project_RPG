// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/GridSlot/RPGEquippedGridSlot.h"
#include "FunctionLibrary/RPGUIFunctionLibrary.h"
#include "UI/Inventory/Hover/RPGHoverItem.h"
#include "Components/Image.h"

void URPGEquippedGridSlot::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	//Super::NativeOnMouseEnter(InGeometry, InMouseEvent);

	SetSlotTextureByHoverItem(EGridSlotState::Occupied, ESlateVisibility::Collapsed);
}

void URPGEquippedGridSlot::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	//Super::NativeOnMouseLeave(InMouseEvent);

	SetSlotTextureByHoverItem(EGridSlotState::Unoccupied, ESlateVisibility::Visible);
}

FReply URPGEquippedGridSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	OnEquippedGridSlotClicked.Broadcast(this, EquipmentTag);
	return FReply::Handled();
}

void URPGEquippedGridSlot::SetSlotTextureByHoverItem(EGridSlotState SlotState, ESlateVisibility InVisibility)
{
	if (!IsAvailiable()) return;
	URPGHoverItem* HoverItem = URPGUIFunctionLibrary::GetHoverItem(GetOwningPlayer());
	if (!IsValid(HoverItem)) return;

	if (HoverItem->GetItemTag().MatchesTag(EquipmentTag))
	{
		SetSlotTexture(SlotState);
		GrayedOutImage->SetVisibility(InVisibility);
	}
}
