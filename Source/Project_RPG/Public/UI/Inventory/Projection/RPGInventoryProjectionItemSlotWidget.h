#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPGInventoryProjectionItemSlotWidget.generated.h"

class UBorder;
class UImage;
class UTextBlock;
class UTexture2D;
class URPGInventoryProjectionViewModel;
struct FStreamableHandle;
enum class ERPGItemSlotPresentationState : uint8;

/** A projected inventory/equipment cell; item commands belong to its ViewModel. */
UCLASS()
class PROJECT_RPG_API URPGInventoryProjectionItemSlotWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	void SetItem(FGuid InItemId, const FText& InLabel,
		URPGInventoryProjectionViewModel* InViewModel, bool bInEquipment = false,
		int32 InContainerSlotIndex = INDEX_NONE);
	bool RequestDefaultAction();
	/** Refresh only status/tint/tooltip; never rebuild the cell or restart icon streaming. */
	void RefreshInteractionState();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent,
		UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry,
		const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry,
		const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	friend class FRPGItemStackSplitInputTest;
	friend class FRPGInventorySplitQuantityInputTest;
	bool CanAcceptInventoryDrop(const UDragDropOperation* InOperation) const;
	void UpdateFrameColor(bool bValidDropTarget = false);
	void RefreshItemVisuals();
	void ApplyIcon(UTexture2D* Texture);
	void CancelIconLoad();
	ERPGItemSlotPresentationState GetCurrentPresentationState() const;

	UPROPERTY(Transient)
	FGuid ItemId;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(Transient)
	TObjectPtr<UImage> IconImage;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> QuantityText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Frame;

	TWeakObjectPtr<URPGInventoryProjectionViewModel> ViewModel;
	FText FallbackLabel;
	FText BaseToolTip;
	TSoftObjectPtr<UTexture2D> IconTexture;
	TSharedPtr<FStreamableHandle> IconLoadHandle;
	uint64 IconLoadGeneration = 0;
	int32 DisplayedQuantity = 0;
	int32 InventorySlotIndex = INDEX_NONE;
	int32 EquipmentSlotIndex = INDEX_NONE;
	bool bEquipment = false;
};
