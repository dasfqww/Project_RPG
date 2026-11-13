// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FieldNotificationId.h"
#include "Type/RPGStructTypes.h"
#include "UI/RPGWidgetBase.h"
#include "UI/MVVM/RPGQuickSlotViewModel.h"
#include "UObject/StrongObjectPtr.h"
#include "RPGQuickSlotWidget.generated.h"

class UImage;
class UTextBlock;
class URPGItemBase;
class UDragQuickSlotItemVisual;
class UQuickSlotComponent;
class UTexture2D;
class URPGInventoryProjectionViewModel;
struct FStreamableHandle;



/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGQuickSlotWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	URPGQuickSlotWidget();
	virtual void BeginDestroy() override;
	
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry,
		const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	/*virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	virtual void NativeOnDragDetected(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;

	virtual bool NativeOnDrop(const FGeometry& InGeometry,
		const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;*/

	UFUNCTION(BlueprintCallable, Category = "QuickSlot",
		meta = (DeprecatedFunction, DeprecationMessage = "Register an ItemId on QuickSlotComponent."))
	void SetSlotItem(URPGItemBase* NewItem, int32 ItemCount);

	void ClearSlotItem();

	void SetInputKeyText(FText InText);

	// 아이템의 이미지 및 수량을 업데이트하는 함수
	//void UpdateSlotUI();

	void SetImageAlpha(UImage* InImage, float InAlpha);

	// 퀵슬롯의 아이템을 사용할 때 호출되는 함수
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use UseAssignedItem."))
	void UseSlotItem(URPGItemBase* UseItem);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void UseAssignedItem();

	UFUNCTION(BlueprintPure, Category = "QuickSlot")
	FGuid GetSlotItemId() const { return SlotItemId; }

	void UpdateQuickSlotItemQuantity(int32 ItemCount);
	void UpdateQuickSlotItemImageAlpha();

private:
	friend struct FRPGQuickSlotClickTestAccess;
	void CancelPendingItemClick();
	void BindToQuickSlot();
	void UnbindFromQuickSlot();
	void RefreshSkillSlotView();
	void BindItemCommandState();
	void RefreshItemCommandState();
	void HandleItemCommandFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);
	TWeakObjectPtr<URPGInventoryProjectionViewModel> ItemCommandViewModel;
	void RefreshItemIcon(TSoftObjectPtr<UTexture2D> Icon);
	void ApplyItemIcon(UTexture2D* Icon);
	void CancelItemIconLoad();
	TSoftObjectPtr<UTexture2D> ItemIconReference;
	TSharedPtr<FStreamableHandle> ItemIconLoadHandle;
	uint64 ItemIconLoadGeneration = 0;
	/** Optional WBP image; the authored material owns the radial appearance. */
	void UpdateSkillCooldownView(float RemainingFraction);
	TStrongObjectPtr<URPGQuickSlotViewModel> SkillViewModel;
	float SkillRefreshElapsed = 0.0f;
	float HoverHighlightAlpha = 0.0f;

	UFUNCTION()
	void HandleSlotChanged(int32 ChangedSlotIndex, const FRPGQuickSlotContent& Content);

protected:
	virtual void NativeOnDeactivated() override;

	/** Transparent center with a soft red edge, shared by item and skill slots. */
	UPROPERTY(EditAnywhere, Category = "QuickSlot|Appearance")
	FLinearColor HoverHighlightColor = FLinearColor(1.0f, 0.025f, 0.012f, 0.85f);

	/** Inward spread in Slate units; kept inside the slot so adjacent cells stay clear. */
	UPROPERTY(EditAnywhere, Category = "QuickSlot|Appearance", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float HoverHighlightWidth = 6.0f;

	UPROPERTY(EditAnywhere, Category = "QuickSlot|Appearance", meta = (ClampMin = "0.01", ClampMax = "1.0", Units = "s"))
	float HoverHighlightFadeDuration = 0.12f;

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

	UPROPERTY(Transient)
	FGuid SlotItemId;

	UPROPERTY(EditAnywhere, Category = "SlotIndex")
	int32 SlotIndex; // 이 슬롯의 인덱스 (예: 첫 번째 슬롯, 두 번째 슬롯)

	UPROPERTY(EditAnywhere, Category = "SlotIndex")
	bool bIsSkillSlot = false;

	UPROPERTY()
	TWeakObjectPtr<UQuickSlotComponent> LinkedQuickSlotComponent;

private:
	// A click may only use the assignment and pointer that began the press.
	FGuid PressedItemId;
	TWeakObjectPtr<UQuickSlotComponent> PressedQuickSlotComponent;
	int32 PressedSlotIndex = INDEX_NONE;
	int32 PressedUserIndex = INDEX_NONE;
	uint32 PressedPointerIndex = 0;
	FVector2D ItemClickScreenOrigin = FVector2D::ZeroVector;

public:
	FORCEINLINE URPGItemBase* GetSlotItem() const { return SlotItem; }
	FORCEINLINE void SetSlotIndex(int Index) { SlotIndex = Index; }
};
