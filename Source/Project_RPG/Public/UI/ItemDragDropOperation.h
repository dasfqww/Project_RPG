// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "ItemDragDropOperation.generated.h"

class URPGItemBase;
class URPGInventoryComponent;
class URPGQuickSlotWidget;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UItemDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<URPGItemBase> SourceItem;
	
	UPROPERTY()
	TObjectPtr<URPGInventoryComponent> SourceInventory;

	/*UPROPERTY()
	TObjectPtr<URPGQuickSlotWidget> SourceQuickSlot;*/
};
