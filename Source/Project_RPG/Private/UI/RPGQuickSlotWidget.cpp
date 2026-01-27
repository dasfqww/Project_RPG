// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGQuickSlotWidget.h"
#include "Item/RPGItemBase.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/ItemDragDropOperation.h"
#include "UI/QuickSlotDragDropOperation.h"
#include "Character/RPGPlayer.h"
#include "Component/UI/QuickSlotComponent.h"
#include "UI/DragQuickSlotItemVisual.h"
#include "UI/DragItemVisual.h"
#include "Component/RPGInventoryComponent.h"
#include "UI/Inventory/Hover/RPGHoverItem.h"

#include "RPGDebugHelper.h"

URPGQuickSlotWidget::URPGQuickSlotWidget()
	:SlotItem(nullptr),
	SlotIndex(0)
{
	
}

void URPGQuickSlotWidget::NativeOnInitialized()
{
	SetInputKeyText(InputText);
	SetImageAlpha(SlotItemImage, 0.f);


}

void URPGQuickSlotWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	//임시로 구현. 나중에 구현 방식 다시 생각해볼것.
	if (SlotItem)
	{
		//UpdateQuickSlotItemQuantity(SlotItem->Quantity);
	}

	UpdateQuickSlotItemImageAlpha();
}

FReply URPGQuickSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	FReply Reply = Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);

	//if (!SlotItem || SlotItem->Quantity <= 0) return Reply.Unhandled();

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return Reply.Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}

	return Reply.Unhandled();
}

FReply URPGQuickSlotWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);

	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	//URPGHoverItem* HoverItem = InventoryGrid->GetHoverItem();

	return FReply::Unhandled();
}

//void URPGQuickSlotWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
//{
//	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
//
//	ClearSlotItem();
//	SetImageAlpha(SlotItemImage, 0.f);
//}
//
//void URPGQuickSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, 
//	const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
//{
//	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
//
//	if (DragVisualClass)
//	{
//		UDragItemVisual* DragVisual = CreateWidget<UDragItemVisual>(this, DragVisualClass);
//		//DragVisual->ItemIcon->SetBrushFromTexture(SlotItem->AssetData.Icon);
//		//DragVisual->ItemBorder->SetBrushColor(ItemBorder->GetBrushColor());
//
//		UQuickSlotDragDropOperation* DragSlotItemOperation = NewObject<UQuickSlotDragDropOperation>();
//		DragSlotItemOperation->SourceItem = SlotItem;
//		DragSlotItemOperation->SourceInventory = SlotItem->OwningInventory;
//		DragSlotItemOperation->SourceQuickSlot = this;
//		DragSlotItemOperation->SourceQuickSlotIndex = SlotIndex;
//
//		DragSlotItemOperation->DefaultDragVisual = DragVisual;
//		DragSlotItemOperation->Pivot = EDragPivot::CenterCenter;
//
//		OutOperation = DragSlotItemOperation;
//	}
//}
//
//bool URPGQuickSlotWidget::NativeOnDrop(const FGeometry& InGeometry,
//	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
//{
//	// 드래그된 아이템을 슬롯에 넣는 처리
//	const UItemDragDropOperation* ItemDragDrop = Cast<UItemDragDropOperation>(InOperation);
//	if (ItemDragDrop && ItemDragDrop->SourceItem)
//	{
//		
//		 
//		//URPGQuickSlotWidget* SourceWidget = Cast<URPGQuickSlotWidget>(ItemDragDrop->SourceQuickSlot);
//		//if (!SourceWidget || SourceWidget == this) return false; // 같은 슬롯이면 무시
//
//		SetImageAlpha(SlotItemImage, 1.f);
//		//SetSlotItem(ItemDragDrop->SourceItem, ItemDragDrop->SourceItem->Quantity);
//
//		ARPGPlayer* Player = Cast<ARPGPlayer>(GetOwningPlayerPawn());
//		if (Player)
//		{
//			UQuickSlotComponent* QuickSlotComponent = Player->GetQuickSlotComponent();
//			if (QuickSlotComponent)
//			{
//				QuickSlotComponent->SetQuickSlotItem(SlotIndex, ItemDragDrop->SourceItem);
//			}
//		}
//
//		return true;
//	}
//
//	//TODO:퀵슬롯 이미지 및 수량 스왑 구현
//	//퀵슬롯 컴포넌트에서 구현해보자.
//	const UQuickSlotDragDropOperation* QuickSlotDragDrop = Cast<UQuickSlotDragDropOperation>(InOperation);
//	if (QuickSlotDragDrop&&QuickSlotDragDrop->SourceInventory&&QuickSlotDragDrop->SourceQuickSlot)
//	{
//		//여기서 스왑 기능을 실행한다.
//		URPGQuickSlotWidget* SourceQuickSlot = Cast<URPGQuickSlotWidget>(QuickSlotDragDrop->SourceQuickSlot);
//		if (!SourceQuickSlot || SourceQuickSlot == this) return false; // 같은 슬롯이면 무시
//
//		ARPGPlayer* Player = Cast<ARPGPlayer>(GetOwningPlayerPawn());
//		if (Player)
//		{
//			UQuickSlotComponent* QuickSlotComponent = Player->GetQuickSlotComponent();
//			if (QuickSlotComponent)
//			{
//				URPGItemBase* SourceItem = SourceQuickSlot->GetSlotItem();
//
//				URPGItemBase* TargetItem = GetSlotItem();
//
//				QuickSlotComponent->SwapQuickSlotItems(SourceQuickSlot->SlotIndex, SlotIndex);
//
//				SourceQuickSlot->SetSlotItem(TargetItem, 0);
//				SetSlotItem(SourceItem, 0);
//			}
//		}
//	}
//
//	return false;
//}

void URPGQuickSlotWidget::SetSlotItem(URPGItemBase* NewItem, int32 ItemCount)
{
	//SlotItem = NewItem;
	//if (SlotItem)
	//{
	//	UTexture2D* ItemTexture = SlotItem->AssetData.Icon;
	//	if (SlotItemImage)
	//	{
	//		SlotItemImage->SetBrushFromTexture(ItemTexture);
	//	}

	//	// 아이템 수량 텍스트 업데이트
	//	if (SlotItemCountText)
	//	{
	//		SlotItemCountText->SetText(FText::AsNumber(ItemCount));
	//	}
	//}
}

void URPGQuickSlotWidget::ClearSlotItem()
{
	SlotItem = nullptr;

	if (SlotItemImage)
	{
		SlotItemImage->SetBrushFromTexture(nullptr);
	}

	// 아이템 수량 텍스트 업데이트
	if (SlotItemCountText)
	{
		SlotItemCountText->SetText(FText::GetEmpty());
	}

	//SetImageAlpha(SlotItemImage, 0.f);
}

void URPGQuickSlotWidget::SetInputKeyText(FText InText)
{
	if (InputKeyText)
	{
		InputKeyText->SetText(InText);
	}
}

//void URPGQuickSlotWidget::UpdateSlotUI()
//{
//	if (SlotItem)
//	{
//		SetSlotItem(SlotItem, SlotItem->Quantity);
//	}
//
//	else
//	{
//		// 아이템이 없다면 슬롯을 비운다.
//		if (SlotItemImage) SlotItemImage->SetBrushFromTexture(nullptr);
//		if (SlotItemCountText) SlotItemCountText->SetText(FText::FromString(TEXT("")));
//	}
//}

void URPGQuickSlotWidget::SetImageAlpha(UImage* InImage, float InAlpha)
{
	FLinearColor NewColor = InImage->GetColorAndOpacity();
	NewColor.A = InAlpha;
	InImage->SetColorAndOpacity(NewColor);
}

void URPGQuickSlotWidget::UseSlotItem(URPGItemBase* UseItem)
{
	SlotItem = UseItem;

	if (!SlotItem)
	{
		Debug::Print("No Item In QuickSlot..");
		return;
	}

	//if (SlotItem&&SlotItem->Quantity>0)
	//{
	//	ARPGPlayer* Player = Cast<ARPGPlayer>(GetOwningPlayerPawn());
	//	//SlotItem->Use(Player);
	//	
	//	//SlotItem->SetQuantity(SlotItem->Quantity -1);
	//	SetSlotItem(SlotItem, SlotItem->Quantity-1);
	//	//UpdateQuickSlotItemQuantity(SlotItem->Quantity - 1);
	//}

	//else if(SlotItem->Quantity <= 0)
	//{
	//	Debug::Print("No Item to use...");
	//	//UpdateQuickSlotItemQuantity(0);
	//}
}

void URPGQuickSlotWidget::UpdateQuickSlotItemQuantity(int32 ItemCount)
{
	if (SlotItem&&SlotItemCountText)
	{
		SlotItemCountText->SetText(FText::AsNumber(ItemCount));
	}
}

void URPGQuickSlotWidget::UpdateQuickSlotItemImageAlpha()
{
	if (!SlotItemImage) return;

	//// 현재 아이템이 없는 경우 투명도 조절
	//if (!SlotItem || SlotItem->Quantity <= 0)
	//{
	//	SetImageAlpha(SlotItemImage, 0.5f); // 투명하게 처리
	//}
	//else
	//{
	//	SetImageAlpha(SlotItemImage, 1.0f); // 원래대로 표시
	//}
}
