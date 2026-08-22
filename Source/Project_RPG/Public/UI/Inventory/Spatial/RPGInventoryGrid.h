// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "Type/RPGEnumTypes.h"
#include "Type/RPGStructTypes.h"
#include "RPGInventoryGrid.generated.h"

class UCanvasPanel;
class URPGGridSlot;
class URPGInventoryComponent;
class URPGItemBase;
struct FItemManifest;
struct FGridFragment;
struct FImageFragment;
class ARPGPickUpBase;
class URPGInventoryItemSlot;
class URPGGridSlot;
class URPGHoverItem;
class UTexture2D;
class URPGItemPopUp;
enum class EGridSlotState : uint8;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInventoryGrid : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/*virtual FReply NativeOnMouseButtonDoubleClick(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent
	) override;*/

	void ConstructGrid();

	//픽업시 해당 아이템 데이터 받아서 추가
	FSlotAvailabilityResult HasSpaceForItem(const ARPGPickUpBase* ItemPickup);

	//아이템 데이터만 받아서 추가
	FSlotAvailabilityResult HasSpaceForItem(const URPGItemBase* Item);
	FSlotAvailabilityResult HasSpaceForItem(const FItemManifest& Manifest);
	void AddItemToIndices(const FSlotAvailabilityResult& Result, URPGItemBase* NewItem);

	void SetVisibleCursor();

	void SetOwningCanvas(UCanvasPanel* OwningCanvas);

	UFUNCTION()
	void AddItem(URPGItemBase* Item);

	UFUNCTION()
	void RemoveItem(URPGItemBase* Item);

	UFUNCTION()
	void RefreshItem(URPGItemBase* Item);

	UFUNCTION()
	void RebuildInventory();
	
	void DropItem();
	bool HasHoverItem() const;

private:

	int32 count = 0;

	TWeakObjectPtr<URPGInventoryComponent> InventoryComponent;
	TWeakObjectPtr<UCanvasPanel> OwningCanvasPanel;

	UFUNCTION()
	void OnGridSlotChanged(int32 GridIndex, const FPointerEvent& MouseEvent, EGridSlotState SlotState);

	UFUNCTION()
	void OnPopUpMenuDrop(int32 Index);

	UFUNCTION()
	void OnPopUpMenuConsume(int32 Index);

	UPROPERTY(EditAnywhere, BlueprintReadOnly,Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	EItemCategory ItemCategory;

	UPROPERTY()
	TArray<TObjectPtr<URPGGridSlot>> GridSlots;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<URPGGridSlot> GridSlotClass;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<URPGInventoryItemSlot> ItemSlotClass;

	TMap<int32, TObjectPtr<URPGInventoryItemSlot>> ItemsInSlot;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 Rows;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 Columns;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float TileSize;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<URPGHoverItem> HoverItemClass;

	UPROPERTY()
	TObjectPtr<URPGHoverItem> HoverItem;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FVector2D ItemPopUpOffset;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<URPGItemPopUp> ItemPopUpClass;

	UPROPERTY()
	TObjectPtr<URPGItemPopUp> ItemPopUp;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TArray<TObjectPtr<UTexture2D>> CursorTextures;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UUserWidget> CursorWidgetClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> CursorWidget;

	FTileParameters TileParams;
	FTileParameters LastTileParams;

	int32 LastHoveredIndex = INDEX_NONE;

	int32 ItemDropIndex = INDEX_NONE;
	FSpaceQueryResult CurrentQueryResult;
	
	bool bMouseWithinCanvas;
	bool bLastMouseWithinCanvas;

	int32 LastHighlightedIndex;
	FIntPoint LastHighlightedDimensions;

	float LastClickTime = 0.f;
	FVector2D LastClickPosition;
	float DoubleClickThreshold = 0.25f; // 0.25초 이내
	float DoubleClickMaxDistance = 10.f; // 10px 이내

	bool MatchesCategory(const URPGItemBase* Item) const;
	//FVector2D GetDrawSize(const FGridFragment* GridFragment) const;
	FVector2D GetDrawSize() const;
	//void SetItemSlotImage(const URPGInventoryItemSlot* ItemSlot, const FGridFragment* GridFragment, const FImageFragment* ImageFragment) const;
	void SetItemSlotImage(const URPGInventoryItemSlot* ItemSlot, const FImageFragment* ImageFragment) const;
	
	void AddItemAtIndex(URPGItemBase* Item, const int32 Index, const bool bStackable, const int32 StackAmount);

	//URPGInventoryItemSlot* CreateSlotItem(URPGItemBase* Item, const bool bStackable, const int32 StackAmount,const FGridFragment* GridFragment, const FImageFragment* ImageFragment, const int32 Index);
	
	URPGInventoryItemSlot* CreateSlotItem(URPGItemBase* Item, const bool bStackable, 
		const int32 StackAmount, const FImageFragment* ImageFragment, const int32 Index);
	/*void AddItemSlotToCanvas(const int32 Index, const FGridFragment* GridFragment, 
		URPGInventoryItemSlot* ItemSlot)const;*/
	void AddItemSlotToCanvas(const int32 Index, URPGInventoryItemSlot* ItemSlot)const;
	void UpdateGridSlots(URPGItemBase* NewItem, const int32 Index, 
		bool bStackableItem, const int32 Quantity);

	/*bool IsIndexClaimed(const TSet<int32>& CheckedIndices, const int32 Index) const;
	bool HasSpaceAtIndex(const URPGGridSlot* GridSlot, const FIntPoint& Dimension, 
		const TSet<int32>& CheckedIndices, TSet<int32>& OutTentativelyClaimed, 
		const FGameplayTag& ItemTag, const int32 MaxQuantity);
	FIntPoint GetItemDimensions(const FItemManifest& Manifest) const;
	bool CheckSlotConstraints(const URPGGridSlot* GridSlot, const URPGGridSlot* SubGridSlot,
		const TSet<int32>& CheckedIndices, TSet<int32>& OutTentativelyClaimed, 
		const FGameplayTag& ItemTag, const int32 MaxQuantity) const;*/
	bool HasValidItem(const URPGGridSlot* GridSlot) const;

	//bool IsUpperLeftSlot(const URPGGridSlot* GridSlot, const URPGGridSlot* SubGridSlot) const;
	bool DoesItemTagMatch(const URPGItemBase* SubItem, const FGameplayTag& ItemTag) const;
	/*bool IsInGridBounds(const int32 StartIndex, const FIntPoint& ItemDimension);
	int32 DetermineFillAmountForSlot(const bool bStackable, const int32 MaxQuantity,
		const int32 AmountToFill, const URPGGridSlot* GridSlot) const;*/
	//int32 GetQuantity(const URPGGridSlot* GridSlot) const;
	
	bool IsCtrlRightClick(const FPointerEvent& MouseEvent)const;
	bool IsLeftClick(const FPointerEvent& MouseEvent) const;
	bool IsRightClick(const FPointerEvent& MouseEvent) const;
	
	//bool IsAltRightClick(const FPointerEvent& MouseEvent) const;
	
	void PickUp(URPGItemBase* ClickedInvenItem, const int32 GridIndex);

	UFUNCTION()
	void AssignAndSetHoverItem(URPGItemBase* Item, const int32 GridIndex,
		const int32 PrevGridIndex, URPGItemPopUp* InItemPopUp);

	void AssignHoverItem(URPGItemBase* Item, const int32 GridIndex, const int32 PrevGridIndex);

	void RemoveItemFromGrid(URPGItemBase* Item, const int32 GridIndex);

	/*void UpdateTileParameters(const FVector2D& CanvasPos, const FVector2D& MousePos);
	FIntPoint CalculateHoveredCoordinates(const FVector2D& CanvasPos, const FVector2D& MousePos) const;
	ETileQuadrant CalculateTileQuadrant(const FVector2D& CanvasPos, const FVector2D& MousePos) const;
	void OnTileParametersUpdated(const FTileParameters& Params);
	FIntPoint CalculateStartingCoordinate(const FIntPoint& Coord,
		const FIntPoint& Dimensions, const ETileQuadrant Quadrant) const;
	FSpaceQueryResult CheckHoverPosition(const FIntPoint& Pos, const FIntPoint& Dimensions);*/
	void UpdateHoveredSlot(const FVector2D& CanvasPos, const FVector2D& MousePos);

	bool CursorExitedCanvas(const FVector2D& BoundaryPos,
		const FVector2D& BoundarySize, const FVector2D& Location);

	//void HighlightSlots(const int32 Index, const FIntPoint& Dimensions);
	void HighlightSlots(const int32 Index);
	//void UnHighlightSlots(const int32 Index, const FIntPoint& Dimensions);
	void UnHighlightSlots(const int32 Index);

	void ChangeHoverType(const int32 Index, const FIntPoint& Dimensions, EGridSlotState	SlotState);

	void PutDownOnIndex(const int32 Index);
	void ClearHoverItem();

	bool IsSameStackable(const URPGItemBase* ClickedInventoryItem) const;
	void SwapWithHoverItem(URPGItemBase* ClickedInventoryItem, const int32 ClickedGridIndex);

	void CreateItemPopUp(const int32 GridIndex);
	

	UUserWidget* GetCursorWidget();

	UFUNCTION()
	void OnSlotItemClicked(int32 GridIndex, const FPointerEvent& MouseEvent);

public:
	FORCEINLINE EItemCategory GetItemCategory() const { return ItemCategory; }
	FORCEINLINE URPGHoverItem* GetHoverItem() const { return HoverItem; }
};
