// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGInventoryItemSlot.generated.h"

class URPGItemBase;
class UDragItemVisual;
class URPGInventoryTooltip;
class UBorder;
class UImage;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSlotItemClicked, int32, GridIndex, const FPointerEvent&, MouseEvent);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInventoryItemSlot : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	URPGInventoryItemSlot();

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent)override;
	
	//virtual void NativeOnDragDetected(const FGeometry& InGeometry,const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)override;
	//void CreateDragVisual(UDragDropOperation*& OutOperation);
	virtual bool NativeOnDrop(const FGeometry& InGeometry, 
		const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation);

	void SetItemReference(URPGItemBase* InItem);
	void SetImageBrush(const FSlateBrush& Brush) const;
	void UpdateItemQuantity(int32 Quantity);

	FOnSlotItemClicked OnSlotItemClicked;

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Item Slot")
	TSubclassOf<UDragItemVisual> DragItemVisualClass;
	
	UPROPERTY(EditDefaultsOnly, Category = "Item Slot")
	TSubclassOf<URPGInventoryTooltip> ToolTipClass;
	
	/*UPROPERTY(VisibleAnywhere, Category = "Item Slot")
	TObjectPtr<URPGItemBase> ItemReference;*/
	
	UPROPERTY(VisibleAnywhere, Category = "Item Slot", meta = (BindWidget))
	TObjectPtr<UBorder> ItemBorder;	

	UPROPERTY(EditDefaultsOnly, Category = "Item Slot", meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemQuantityText;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;

	int32 GridIndex;
	FIntPoint GridDimensions;

	TWeakObjectPtr<URPGItemBase> InvenItem;

	bool bIsStackable = false;

public:
	FORCEINLINE UImage* GetItemIcon() const{ return ItemIcon; }
	
	FORCEINLINE URPGItemBase* GetItemReference() const{ return InvenItem.Get(); }
	
	FORCEINLINE bool IsStacklable() const { return bIsStackable; }
	FORCEINLINE void SetIsStackable(bool bStackable) { bIsStackable = bStackable; }
	FORCEINLINE int32 GetGridIndex() const{ return GridIndex; }
	FORCEINLINE void SetGridIndex(int32 Index) { GridIndex = Index; }
	FORCEINLINE FIntPoint GetGridDimensions()const { return GridDimensions; }
	FORCEINLINE void SetGridDimensions(const FIntPoint& Dimensions){ GridDimensions = Dimensions; }
};
