// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGInventoryItemSlot.h"
#include "UI/RPGInventoryTooltip.h"
#include "Item/RPGItemBase.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/DragItemVisual.h"
#include "UI/ItemDragDropOperation.h"
#include "RPGFunctionLibrary.h"

URPGInventoryItemSlot::URPGInventoryItemSlot()
{
}

void URPGInventoryItemSlot::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    //if (ToolTipClass)
    //{
    //    URPGInventoryTooltip* ToolTip = CreateWidget<URPGInventoryTooltip>(this, ToolTipClass);

    //    SetToolTip(ToolTip);
    //    //ToolTip->SetInventorySlotBeingHovered(this);
    //}
}

void URPGInventoryItemSlot::NativeConstruct()
{
    Super::NativeConstruct();

   /* if (ItemReference)
    {
        switch (ItemReference->ItemGrade)
        {
        case EItemGrade::Common:
            ItemBorder->SetBrushColor(FLinearColor::Gray);
            break;
        case EItemGrade::Advanced:
            ItemBorder->SetBrushColor(FLinearColor::Green);
            break;
        case EItemGrade::Rare:
            ItemBorder->SetBrushColor(FLinearColor::Blue);
            break;
        case EItemGrade::Hero:
            ItemBorder->SetBrushColor(FLinearColor::Red);
            break;
        case EItemGrade::Legend:
            ItemBorder->SetBrushColor(FLinearColor::Yellow);
            break;
        }

        ItemIcon->SetBrushFromTexture(ItemReference->AssetData.Icon);

        if (ItemReference->NumericData.bIsStackable)
        {
            ItemQuantity->SetText(FText::AsNumber(ItemReference->Quantity));
        }
        else
        {
            ItemQuantity->SetVisibility(ESlateVisibility::Collapsed);
        }
    }*/
}

FReply URPGInventoryItemSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    OnSlotItemClicked.Broadcast(GridIndex, InMouseEvent);
    return FReply::Handled();
    /* if (InMouseEvent.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        return Reply.Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
    }

    return Reply.Unhandled();*/
}

void URPGInventoryItemSlot::NativeOnMouseEnter(const FGeometry& MyGeometry,
    const FPointerEvent& MouseEvent)
{
    URPGFunctionLibrary::ItemHovered(GetOwningPlayer(), InvenItem.Get());
}

void URPGInventoryItemSlot::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
    //Super::NativeOnMouseLeave(InMouseEvent);

    URPGFunctionLibrary::ItemUnhovered(GetOwningPlayer());
}

//void URPGInventoryItemSlot::NativeOnDragDetected(const FGeometry& InGeometry, 
//    const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
//{
//    Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
//
//    CreateDragVisual(OutOperation);
//}

//void URPGInventoryItemSlot::CreateDragVisual(UDragDropOperation*& OutOperation)
//{
//   /* if (DragItemVisualClass)
//    {
//        UDragItemVisual* DragVisual = CreateWidget<UDragItemVisual>(this, DragItemVisualClass);
//        DragVisual->ItemIcon->SetBrushFromTexture(ItemReference->AssetData.Icon);
//        DragVisual->ItemBorder->SetBrushColor(ItemBorder->GetBrushColor());
//
//        UItemDragDropOperation* DragItemOperation = NewObject<UItemDragDropOperation>();
//        DragItemOperation->SourceItem = ItemReference;
//        DragItemOperation->SourceInventory = ItemReference->OwningInventory;
//
//        DragItemOperation->DefaultDragVisual = DragVisual;
//        DragItemOperation->Pivot = EDragPivot::CenterCenter;
//
//        OutOperation = DragItemOperation;
//    }*/
//}

bool URPGInventoryItemSlot::NativeOnDrop(const FGeometry& InGeometry, 
    const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

void URPGInventoryItemSlot::SetItemReference(URPGItemBase* InItem)
{
    InvenItem = InItem;
}

void URPGInventoryItemSlot::SetImageBrush(const FSlateBrush& Brush) const
{
    ItemIcon->SetBrush(Brush);
}

void URPGInventoryItemSlot::UpdateItemQuantity(int32 Quantity)
{
    if (Quantity>0)
    {
        ItemQuantityText->SetVisibility(ESlateVisibility::Visible);
        ItemQuantityText->SetText(FText::AsNumber(Quantity));
    }

    else
    {
        ItemQuantityText->SetVisibility(ESlateVisibility::Collapsed);
    }
}
