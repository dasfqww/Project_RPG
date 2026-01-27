// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGInventoryPanel.h"
#include "Character/RPGPlayer.h"
#include "Controller/RPGPlayerController.h"
#include "Component/RPGInventoryComponent.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "UI/RPGInventoryItemSlot.h"
#include "UI/ItemDragDropOperation.h"
#include "UI/RPGInventoryTooltip.h"

void URPGInventoryPanel::RefreshInventory()
{
	/* Deprecated logic removed
	if (InventoryReference && InventorySlotClass)
	{
		InventoryPanel->ClearChildren();

		for (URPGItemBase* const& InventoryItem : InventoryReference->GetInventoryContents())
		{
			URPGInventoryItemSlot* ItemSlot = CreateWidget<URPGInventoryItemSlot>(this, InventorySlotClass);
			ItemSlot->SetItemReference(InventoryItem);

			InventoryPanel->AddChildToWrapBox(ItemSlot);
		}

		SetInfoText();
	}
	*/
}

void URPGInventoryPanel::SetInfoText()
{
	/* Deprecated logic removed
	const FString WeightInfoValue{
		FString::SanitizeFloat(InventoryReference->GetInventoryTotalWeight()) + "/"
		+ FString::SanitizeFloat(InventoryReference->GetWeightCapacity())
	};

	const FString CapacityInfoValue{
		FString::FromInt(InventoryReference->GetInventoryContents().Num()) + "/"
		+ FString::FromInt(InventoryReference->GetSlotsCapacity())
	};

	WeightInfo->SetText(FText::FromString(WeightInfoValue));
	CapacityInfo->SetText(FText::FromString(CapacityInfoValue));
	*/
}

void URPGInventoryPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	PlayerCharacter = Cast<ARPGPlayer>(GetOwningPlayerPawn());
	if (PlayerCharacter)
	{
		/*InventoryReference = PlayerCharacter->GetRPGInventory();
		if (InventoryReference)
		{
			InventoryReference->OnInventoryUpdated.AddUObject(this, &ThisClass::RefreshInventory);
			SetInfoText();
		}*/
	}
}

bool URPGInventoryPanel::NativeOnDrop(const FGeometry& InGeometry, 
	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	const UItemDragDropOperation* ItemDragDrop = Cast<UItemDragDropOperation>(InOperation);

	if (ItemDragDrop->SourceItem && InventoryReference)
	{
		UE_LOG(LogTemp, Warning, TEXT("Detected an item drop on InventoryPanel."))

			// returning true will stop the drop operation at this widget
			return true;
	}
	// returning false will cause the drop operation to fall through to underlying widgets (if any)
	return false;
}
