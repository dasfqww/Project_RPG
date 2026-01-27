// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/RPGInventoryBase.h"
#include "RPGSpatialInventory.generated.h"

class URPGInventoryTooltip;
class URPGInventoryGrid;
class UWidgetSwitcher;
class UButton;
class ARPGPickUpBase;
class UCanvasPanel;
class URPGHoverItem;
struct FSlotAvailabilityResult;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGSpatialInventory : public URPGInventoryBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual FSlotAvailabilityResult HasSpaceForItem(ARPGPickUpBase* ItemPickup) const override;

	virtual void OnItemHovered(URPGItemBase* Item) override;
	virtual void OnItemUnHovered() override;
	virtual bool HasHoverItem() const override;
	virtual URPGHoverItem* GetHoverItem() const override;

protected:
	UFUNCTION(BlueprintCallable)
	void ShowEquipInven();

	UFUNCTION(BlueprintCallable)
	void ShowConsumeInven();

	UFUNCTION(BlueprintCallable)
	void ShowCraftInven();

	void DisableButton(UButton* Button);
	void SetActiveGrid(URPGInventoryGrid* Grid, UButton* Button);
	void SetItemTooltipSizeAndPosition(URPGInventoryTooltip* Description, UCanvasPanel* Canvas) const;

	TWeakObjectPtr<URPGInventoryGrid> ActiveGrid;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGInventoryGrid> Grid_Equip;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGInventoryGrid> Grid_Consume;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGInventoryGrid> Grid_Craft;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Equip;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Consume;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Craft;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<URPGInventoryTooltip> ItemToolTipClass;

	UPROPERTY()
	TObjectPtr<URPGInventoryTooltip> ItemToolTip;

	FTimerHandle TooltipTimer;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float TooltipTimerDelay = 0.5f;

	URPGInventoryTooltip* GetItemTooltip();
};
