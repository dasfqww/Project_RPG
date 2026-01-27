// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGInventoryPanel.generated.h"

class UWrapBox;
class UTextBlock;
class ARPGPlayer;
class URPGInventoryComponent;
class URPGInventoryItemSlot;
class URPGInventoryTooltip;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInventoryPanel : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	void RefreshInventory();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> InventoryPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> WeightInfo;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CapacityInfo;

	UPROPERTY()
	TObjectPtr<ARPGPlayer> PlayerCharacter;

	UPROPERTY()
	TObjectPtr<URPGInventoryComponent> InventoryReference;

	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<URPGInventoryItemSlot> InventorySlotClass;
	
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<URPGInventoryTooltip> ToolTipClass;


protected:
	void SetInfoText();
	virtual void NativeOnInitialized() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry,
		const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};
