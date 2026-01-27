// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Inventory/Equipment/RPGEquipmentWidget.h"
#include "Blueprint/WidgetTree.h"
#include "UI/GridSlot/RPGEquippedGridSlot.h"
#include "UI/Inventory/Hover/RPGHoverItem.h"
#include "UI/Inventory/Spatial/RPGSpatialInventory.h"
#include "Item/RPGItemBase.h"

void URPGEquipmentWidget::NativeOnInitialized()
{
	WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			URPGEquippedGridSlot* EquippedGridSlot = Cast<URPGEquippedGridSlot>(Widget);
			if (IsValid(EquippedGridSlot))
			{
				EquippedGridSlots.Add(EquippedGridSlot);
				EquippedGridSlot->
					OnEquippedGridSlotClicked.AddDynamic(this, &ThisClass::EquippedGridSlotClicked);
			}
		});

	SpatialInventory = CreateWidget<URPGSpatialInventory>(this, SpatialInventoryClass);
}

void URPGEquipmentWidget::NativeConstruct()
{
	Super::NativeConstruct();

	
}

void URPGEquipmentWidget::EquippedGridSlotClicked
	(URPGEquippedGridSlot* EquippedGridSlot, const FGameplayTag& EquipmentTag)
{
	if (!CanEquipHoverItem(EquippedGridSlot, EquipmentTag)) return;

}

bool URPGEquipmentWidget::CanEquipHoverItem(URPGEquippedGridSlot* EquippedGridSlot,
	const FGameplayTag& EquipmentTypeTag) const
{
	if (!IsValid(EquippedGridSlot) || EquippedGridSlot->GetInvenItem().IsValid()) return false;

	URPGHoverItem* HoverItem = SpatialInventory->GetHoverItem();
	if (!IsValid(HoverItem)) return false;

	URPGItemBase* HeldItem = HoverItem->GetInvenItem();

	return SpatialInventory->HasHoverItem() &&
		!HoverItem->IsStackable() &&
		HeldItem->GetItemManifest().GetItemCategory() == EItemCategory::Equip &&
		HeldItem->GetItemManifest().GetItemTag().MatchesTag(EquipmentTypeTag);
}
