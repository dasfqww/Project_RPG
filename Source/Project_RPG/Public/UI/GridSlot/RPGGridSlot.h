// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGGridSlot.generated.h"

class UImage;
class URPGItemBase;
class URPGItemPopUp;

UENUM(BlueprintType)
enum class EGridSlotState : uint8
{
	Unoccupied,
	Occupied,
	Selected,
	GrayedOut
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGridSlotChanged, int32, GridIndex, const FPointerEvent&, MouseEvent, EGridSlotState, SlotState);


/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGGridSlot : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	void SetSlotTexture(EGridSlotState SlotState);
	void SetInvenItem(URPGItemBase* Item);
	
	URPGItemPopUp* GetItemPopUp()const;
	void SetItemPopUp(URPGItemPopUp* PopUp);

	FOnGridSlotChanged OnGridSlotChanged;

private:
	int32 TileIndex=INDEX_NONE;
	int32 Quantity=0;
	int32 UpperLeftIndex = INDEX_NONE;

	bool bAvailiable = true;

	TWeakObjectPtr<URPGItemBase> InvenItem;

	TWeakObjectPtr<URPGItemPopUp> ItemPopUp;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> GridSlotImage;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_Unoccupied;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_Occupied;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_Selected;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_GrayedOut;

	EGridSlotState GridSlotState;

	UFUNCTION()
	void OnItemPopUpDestruct(UUserWidget* Menu);

public:
	FORCEINLINE int32 GetTileIndex() const { return TileIndex; }
	FORCEINLINE void SetTileIndex(int Index) { TileIndex = Index; }
	FORCEINLINE int32 GetQuantity() const { return Quantity; }
	FORCEINLINE void SetQuantity(int Amount) { Quantity = Amount; }
	FORCEINLINE int32 GetUpperLeftIndex() const { return UpperLeftIndex; }
	FORCEINLINE void SetUpperLeftIndex(int Index) { UpperLeftIndex = Index; }
	FORCEINLINE bool IsAvailiable() const { return bAvailiable; }
	FORCEINLINE void SetAvailable(bool bIsAvailiable) { bAvailiable = bIsAvailiable; }
	FORCEINLINE EGridSlotState GetGridSlotState() const { return GridSlotState; }
	FORCEINLINE TWeakObjectPtr<URPGItemBase> GetInvenItem() const { return InvenItem; }

};