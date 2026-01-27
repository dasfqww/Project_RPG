// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGMainMenuWidget.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGPlayer.h"
#include "Controller/RPGPlayerController.h"
#include "UI/ItemDragDropOperation.h"
#include "UI/QuickSlotDragDropOperation.h"
#include "UI/RPGQuickSlotWidget.h"

void URPGMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void URPGMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	PlayerCharacter = Cast<ARPGPlayer>(GetOwningPlayerPawn());
}

bool URPGMainMenuWidget::NativeOnDrop(const FGeometry& InGeometry, 
	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	//return NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	
	const UItemDragDropOperation* ItemDragDrop = Cast<UItemDragDropOperation>(InOperation);

	if (PlayerCharacter&& ItemDragDrop&&ItemDragDrop->SourceItem)
	{
		if (ARPGPlayerController* PC=Cast<ARPGPlayerController>(PlayerCharacter->Controller))
		{
			//PC->DropItem(ItemDragDrop->SourceItem, ItemDragDrop->SourceItem->Quantity);
			return true;
		}
	}

	const UQuickSlotDragDropOperation* QuickSlotDragDrop = Cast<UQuickSlotDragDropOperation>(InOperation);

	if (QuickSlotDragDrop&&QuickSlotDragDrop->SourceInventory&&QuickSlotDragDrop->SourceQuickSlot)
	{
		QuickSlotDragDrop->SourceQuickSlot->ClearSlotItem();
	}

	return false;
}
