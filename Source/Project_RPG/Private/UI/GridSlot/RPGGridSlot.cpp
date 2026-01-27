// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/GridSlot/RPGGridSlot.h"
#include "Item/RPGItemBase.h"
#include "Components/Image.h"
#include "UI/PopUp/RPGItemPopUp.h"

void URPGGridSlot::NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Super::NativeOnMouseEnter(MyGeometry, MouseEvent);

	OnGridSlotChanged.Broadcast(TileIndex, MouseEvent, EGridSlotState::Occupied);
}

void URPGGridSlot::NativeOnMouseLeave(const FPointerEvent& MouseEvent)
{
	Super::NativeOnMouseLeave(MouseEvent);

	OnGridSlotChanged.Broadcast(TileIndex, MouseEvent, EGridSlotState::Unoccupied);
}

FReply URPGGridSlot::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	OnGridSlotChanged.Broadcast(TileIndex, MouseEvent, EGridSlotState::Selected);

	return FReply::Handled();
}

void URPGGridSlot::SetSlotTexture(EGridSlotState SlotState)
{
	GridSlotState = SlotState;

	switch (SlotState)
	{
	case EGridSlotState::Unoccupied:
		GridSlotImage->SetBrush(Brush_Unoccupied);
		break;
	case EGridSlotState::Occupied:
		GridSlotImage->SetBrush(Brush_Occupied);
		break;
	case EGridSlotState::Selected:
		GridSlotImage->SetBrush(Brush_Selected);
		break;
	case EGridSlotState::GrayedOut:
		GridSlotImage->SetBrush(Brush_GrayedOut);
		break;
	default:
		break;
	}
	
	
}

void URPGGridSlot::SetInvenItem(URPGItemBase* Item)
{
	InvenItem = Item;
}

URPGItemPopUp* URPGGridSlot::GetItemPopUp() const
{
	return ItemPopUp.Get();
}

void URPGGridSlot::SetItemPopUp(URPGItemPopUp* PopUp)
{
	ItemPopUp = PopUp;
	ItemPopUp->SetGridIndex(GetTileIndex());
	ItemPopUp->OnNativeDestruct.AddUObject(this, &ThisClass::OnItemPopUpDestruct);
}

void URPGGridSlot::OnItemPopUpDestruct(UUserWidget* Menu)
{
	ItemPopUp.Reset();
}
