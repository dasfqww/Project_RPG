// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGItemPopUp.generated.h"

class UButton;
class USizeBox;
class URPGItemSplitPopUp;
class URPGGridSlot;
class URPGInventoryItemSlot;
class URPGItemBase;
class URPGHoverItem;

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnPopUpMenuDrop, int32, Index);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnPopUpMenuConsume, int32, Index);
DECLARE_DYNAMIC_DELEGATE_FourParams(FOnAssignHoverItem, URPGItemBase*, Item, const int32, GridIndex, const int32, PrevGridIndex, URPGItemPopUp*, ItemPopUp);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGItemPopUp : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	UFUNCTION()
	void PopUpItemSplitWidget();

	UFUNCTION()
	void DropItem();

	UFUNCTION()
	void ConsumeItem();

	UFUNCTION()
	void OnPopUpMenuSplit(int32 SplitAmount, int32 Index);

	FOnPopUpMenuDrop OnDrop;
	FOnPopUpMenuConsume OnConsume;
	FOnAssignHoverItem OnAssignHoverItem;

	void CollapseSplitButton() const;
	void CollapseConsumeButton() const;
	FVector2D GetBoxSize() const;

private:
	UPROPERTY(EditAnywhere, Category = "PopUp")
	TSubclassOf<URPGItemSplitPopUp> ItemSplitPopUpClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SplitButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> DropButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ConsumeButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> RootSizeBox;

	int32 GridIndex = INDEX_NONE;

	int32 SliderMax;

	UPROPERTY()
	TArray<TObjectPtr<URPGGridSlot>> GridSlots;

	TMap<int32, TObjectPtr<URPGInventoryItemSlot>> ItemsInSlot;

	UPROPERTY()
	TObjectPtr<URPGHoverItem> HoverItem;

public:
	FORCEINLINE int32 GetGridIndex() const { return GridIndex; }
	FORCEINLINE void SetGridIndex(int32 Index) { GridIndex = Index; }
	FORCEINLINE void SetSliderMax(int32 Value) { SliderMax = Value; }
	FORCEINLINE void SetHoverItem(URPGHoverItem* InHoverItem) { HoverItem = InHoverItem; }
	FORCEINLINE void SetGridSlots(const TArray<TObjectPtr<URPGGridSlot>>& InGridSlots) { GridSlots = InGridSlots; }
	FORCEINLINE void SetItemsInSlot(const TMap<int32, TObjectPtr<URPGInventoryItemSlot >>& InItemsMap) { ItemsInSlot = InItemsMap; }
};
