// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Inventory/Hover/RPGHoverItem.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Item/RPGItemBase.h"

void URPGHoverItem::SetImageBrush(const FSlateBrush& Brush) const
{
	ItemIcon->SetBrush(Brush);
}

void URPGHoverItem::UpdateQuantity(int32 InQuantity)
{
	Quantity = InQuantity;
	if (InQuantity >0)
	{
		QuantityText->SetText(FText::AsNumber(InQuantity));
		QuantityText->SetVisibility(ESlateVisibility::Visible);
	}

	else
	{
		QuantityText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

FGameplayTag URPGHoverItem::GetItemTag() const
{
	if (InvenItem.IsValid())
	{
		return InvenItem->GetItemManifest().GetItemTag();
	}

	return FGameplayTag();
}

void URPGHoverItem::SetIsStackable(bool bStackable)
{
	bIsStackable = bStackable;
	if (!bIsStackable)
	{
		QuantityText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

URPGItemBase* URPGHoverItem::GetInvenItem() const
{
	return InvenItem.Get();
}

void URPGHoverItem::SetInvenItem(URPGItemBase* Item)
{
	InvenItem = Item;
}
