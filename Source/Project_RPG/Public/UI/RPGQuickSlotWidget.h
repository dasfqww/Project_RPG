// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGQuickSlotWidget.generated.h"

class UImage;
class UTextBlock;
class URPGItemBase;
class UDragQuickSlotItemVisual;



/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGQuickSlotWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	URPGQuickSlotWidget();
	
	virtual void NativeOnInitialized() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	/*virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	virtual void NativeOnDragDetected(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;

	virtual bool NativeOnDrop(const FGeometry& InGeometry,
		const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;*/

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void SetSlotItem(URPGItemBase* NewItem, int32 ItemCount);

	void ClearSlotItem();

	void SetInputKeyText(FText InText);

	// 아이템의 이미지 및 수량을 업데이트하는 함수
	//void UpdateSlotUI();

	void SetImageAlpha(UImage* InImage, float InAlpha);

	// 퀵슬롯의 아이템을 사용할 때 호출되는 함수
	UFUNCTION(BlueprintCallable)
	void UseSlotItem(URPGItemBase* UseItem);

	void UpdateQuickSlotItemQuantity(int32 ItemCount);
	void UpdateQuickSlotItemImageAlpha();

protected:
	// 슬롯에 아이템을 배치할 텍스트/이미지 블록
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SlotItemImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SlotItemCountText;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> InputKeyText;

	UPROPERTY(EditAnywhere, Category = "Input")
	FText InputText;

	UPROPERTY(EditDefaultsOnly, Category = "Drag Visual")
	TSubclassOf<UDragItemVisual> DragVisualClass;

	UPROPERTY()
	TObjectPtr<URPGItemBase> SlotItem; // 슬롯에 배치된 아이템 객체

	UPROPERTY(EditAnywhere, Category = "SlotIndex")
	int32 SlotIndex; // 이 슬롯의 인덱스 (예: 첫 번째 슬롯, 두 번째 슬롯)

public:
	FORCEINLINE URPGItemBase* GetSlotItem() const { return SlotItem; }
	FORCEINLINE void SetSlotIndex(int Index) { SlotIndex = Index; }
};
