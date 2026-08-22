// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGQuickSlotWidget.h"
#include "Item/RPGItemBase.h"
#include "Item/Fragment/RPGItemFragment.h"
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
#include "Controller/RPGPlayerController.h"

#include "RPGGameplayTags.h"
#include "RPGDebugHelper.h"

namespace
{
FText ResolveQuickSlotInputKeyText(
	const UQuickSlotComponent* Component,
	bool bIsSkillSlot,
	int32 SlotIndex,
	const FText& FallbackText)
{
	const TCHAR* SlotType = bIsSkillSlot ? TEXT("Skill") : TEXT("Item");
	const FString TagName = FString::Printf(
		TEXT("InputTag.Quick%s.%d"),
		SlotType,
		SlotIndex + 1);
	const FGameplayTag InputTag =
		FGameplayTag::RequestGameplayTag(FName(*TagName), false);

	const APawn* OwnerPawn = Component ? Cast<APawn>(Component->GetOwner()) : nullptr;
	const ARPGPlayerController* PlayerController =
		OwnerPawn ? Cast<ARPGPlayerController>(OwnerPawn->GetController()) : nullptr;
	if (PlayerController && InputTag.IsValid())
	{
		const FKey Key = PlayerController->GetCurrentKeyForTag(InputTag);
		if (Key.IsValid())
		{
			return Key.GetDisplayName(false);
		}
	}

	return FallbackText;
}
}

URPGQuickSlotWidget::URPGQuickSlotWidget()
	:SlotItem(nullptr),
	SlotIndex(0)
{
	
}

void URPGQuickSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetInputKeyText(InputText);
	SetImageAlpha(SlotItemImage, 0.f);
}

void URPGQuickSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindToQuickSlot();
}

void URPGQuickSlotWidget::NativeDestruct()
{
	UnbindFromQuickSlot();
	Super::NativeDestruct();
}

void URPGQuickSlotWidget::BindToQuickSlot()
{
	UnbindFromQuickSlot();

	ARPGPlayer* Player = Cast<ARPGPlayer>(GetOwningPlayerPawn());
	UQuickSlotComponent* Component = Player ? Player->GetQuickSlotComponent() : nullptr;
	if (!Component)
	{
		ClearSlotItem();
		return;
	}

	LinkedQuickSlotComponent = Component;
	SetInputKeyText(ResolveQuickSlotInputKeyText(
		Component, bIsSkillSlot, SlotIndex, InputText));
	if (bIsSkillSlot)
	{
		Component->OnSkillSlotChanged.AddUniqueDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
		if (const FRPGQuickSlotContent* Content =
			Component->GetSkillSlotContent(SlotIndex))
		{
			HandleSlotChanged(SlotIndex, *Content);
		}
	}
	else
	{
		Component->OnItemSlotChanged.AddUniqueDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
		Component->OnQuickSlotQuantityChanged.AddUniqueDynamic(
			this, &URPGQuickSlotWidget::HandleQuantityChanged);
		if (const FRPGQuickSlotContent* Content =
			Component->GetItemSlotContent(SlotIndex))
		{
			HandleSlotChanged(SlotIndex, *Content);
		}
	}
}

void URPGQuickSlotWidget::UnbindFromQuickSlot()
{
	if (UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get())
	{
		Component->OnSkillSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
		Component->OnItemSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
		Component->OnQuickSlotQuantityChanged.RemoveDynamic(
			this, &URPGQuickSlotWidget::HandleQuantityChanged);
	}

	LinkedQuickSlotComponent.Reset();
}

void URPGQuickSlotWidget::HandleSlotChanged(
	int32 ChangedSlotIndex, const FRPGQuickSlotContent& Content)
{
	if (ChangedSlotIndex != SlotIndex)
	{
		return;
	}

	if (Content.Item)
	{
		SetSlotItem(Content.Item, Content.Item->GetTotalQuantity());
		return;
	}

	SlotItem = nullptr;
	if (Content.AbilityTag.IsValid())
	{
		UTexture2D* SkillIcon = LinkedQuickSlotComponent.IsValid()
			? LinkedQuickSlotComponent->GetSkillIcon(Content.AbilityTag)
			: nullptr;
		if (SlotItemImage)
		{
			SlotItemImage->SetBrushFromTexture(SkillIcon);
		}
		if (SlotItemCountText)
		{
			SlotItemCountText->SetText(FText::GetEmpty());
		}
		SetImageAlpha(SlotItemImage, SkillIcon ? 1.f : 0.f);
		return;
	}

	ClearSlotItem();
}

void URPGQuickSlotWidget::HandleQuantityChanged(URPGItemBase* Item, int32 NewQuantity)
{
	if (Item != SlotItem)
	{
		return;
	}

	if (NewQuantity > 0)
	{
		UpdateQuickSlotItemQuantity(NewQuantity);
	}
	else
	{
		ClearSlotItem();
	}
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
	if (!IsValid(NewItem) || ItemCount <= 0)
	{
		ClearSlotItem();
		return;
	}

	SlotItem = NewItem;
	UTexture2D* ItemTexture = nullptr;
	if (const FImageFragment* ImageFragment =
		GetFragment<FImageFragment>(
			NewItem, RPGGameplayTags::Fragment_IconFragment))
	{
		ItemTexture = ImageFragment->GetIcon();
	}

	if (SlotItemImage)
	{
		SlotItemImage->SetBrushFromTexture(ItemTexture);
	}

	if (SlotItemCountText)
	{
		SlotItemCountText->SetText(FText::AsNumber(ItemCount));
	}

	SetImageAlpha(SlotItemImage, 1.f);
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

	SetImageAlpha(SlotItemImage, 0.f);
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
	if (!InImage)
	{
		return;
	}

	FLinearColor NewColor = InImage->GetColorAndOpacity();
	NewColor.A = InAlpha;
	InImage->SetColorAndOpacity(NewColor);
}

void URPGQuickSlotWidget::UseSlotItem(URPGItemBase* UseItem)
{
	if (!IsValid(UseItem) || UseItem != SlotItem)
	{
		Debug::Print("No Item In QuickSlot..");
		return;
	}

	if (UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get())
	{
		Component->UseItemSlot(SlotIndex, GetOwningPlayer());
	}
}

void URPGQuickSlotWidget::UpdateQuickSlotItemQuantity(int32 ItemCount)
{
	if (SlotItem&&SlotItemCountText)
	{
		SlotItemCountText->SetText(
			ItemCount > 0 ? FText::AsNumber(ItemCount) : FText::GetEmpty());
	}
}

void URPGQuickSlotWidget::UpdateQuickSlotItemImageAlpha()
{
	SetImageAlpha(SlotItemImage, IsValid(SlotItem) ? 1.f : 0.f);
}
