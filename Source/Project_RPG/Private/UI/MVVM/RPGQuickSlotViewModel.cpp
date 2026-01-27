// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGQuickSlotViewModel.h"
#include "Item/RPGItemBase.h"
#include "Component/UI/QuickSlotComponent.h"

void URPGQuickSlotViewModel::Initialize(int32 InSlotIndex, UQuickSlotComponent* InComponent)
{
	if (!InComponent) return;

	TargetSlotIndex = InSlotIndex;
	LinkedComponent = InComponent;

	// 컴포넌트의 슬롯 변경 및 수량 변경 델리게이트 구독
	InComponent->OnQuickSlotChanged.AddDynamic(this, &URPGQuickSlotViewModel::HandleSlotChanged);
	InComponent->OnQuickSlotQuantityChanged.AddDynamic(this, &URPGQuickSlotViewModel::HandleQuantityChanged);

	// 입력 키 텍스트 초기 설정 (예: 1, 2, 3, 4...)
	SetInputKeyText(FText::AsNumber(InSlotIndex + 1));

	// 초기 데이터 반영
	UpdateFromItem(InComponent->GetItemInSlot(InSlotIndex));
}

void URPGQuickSlotViewModel::HandleSlotChanged(int32 SlotIndex, URPGItemBase* NewItem)
{
	if (SlotIndex == TargetSlotIndex)
	{
		UpdateFromItem(NewItem);
	}
}

void URPGQuickSlotViewModel::HandleQuantityChanged(URPGItemBase* Item, int32 NewQuantity)
{
	// 현재 이 슬롯에 들어있는 아이템의 수량이 변한 경우에만 갱신
	if (LinkedComponent.IsValid())
	{
		URPGItemBase* CurrentItem = LinkedComponent->GetItemInSlot(TargetSlotIndex);
		if (CurrentItem == Item)
		{
			if (NewQuantity > 0)
			{
				SetQuantityText(FText::AsNumber(NewQuantity));
			}
			else
			{
				// 수량이 0이 되면 슬롯 비우기
				UpdateFromItem(nullptr);
			}
		}
	}
}

void URPGQuickSlotViewModel::UpdateFromItem(URPGItemBase* InItem)
{
	if (InItem && InItem->GetTotalQuantity() > 0)
	{
		// 아이템이 있을 때
		// SetItemIcon(InItem->GetIcon()); // 아이템 아이콘 설정 로직 필요
		SetQuantityText(FText::AsNumber(InItem->GetTotalQuantity()));
		SetIsSlotActive(true);
	}
	else
	{
		// 아이템이 없거나 수량이 0일 때
		SetItemIcon(nullptr);
		SetQuantityText(FText::GetEmpty());
		SetIsSlotActive(false);
	}
}

void URPGQuickSlotViewModel::SetItemIcon(UTexture2D* InIcon)
{
	UE_MVVM_SET_PROPERTY_VALUE(ItemIcon, InIcon);
}

void URPGQuickSlotViewModel::SetQuantityText(FText InText)
{
	UE_MVVM_SET_PROPERTY_VALUE(QuantityText, InText);
}

void URPGQuickSlotViewModel::SetIsSlotActive(bool bInActive)
{
	UE_MVVM_SET_PROPERTY_VALUE(bIsSlotActive, bInActive);
}

void URPGQuickSlotViewModel::SetInputKeyText(FText InText)
{
	UE_MVVM_SET_PROPERTY_VALUE(InputKeyText, InText);
}
