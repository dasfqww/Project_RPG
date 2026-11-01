#pragma once

#include "CoreMinimal.h"
#include "FieldNotificationId.h"
#include "Types/SlateEnums.h"
#include "UI/RPGWidgetBase.h"
#include "RPGInventoryProjectionWidget.generated.h"

class UTextBlock;
class UButton;
class UUniformGridPanel;
class UVerticalBox;
class UEditableTextBox;
class URPGInventoryProjectionViewModel;

/**
 * Thin item view that renders the owner-only authoritative projection.
 *
 * The widget depends only on the presentation ViewModel. It never reads the
 * replicated projection component or sends item RPCs directly, keeping UMG
 * layout concerns separate from item transport and transaction concerns.
 */
UCLASS()
class PROJECT_RPG_API URPGInventoryProjectionWidget : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	FSimpleMulticastDelegate OnWindowClosed;
	/** Presentation only; pending item commands remain owned by the shared model. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|UI")
	void RequestCloseWindow();

	UFUNCTION(BlueprintPure, Category = "RPG|Item|UI")
	URPGInventoryProjectionViewModel* GetInventoryViewModel() const
	{
		return InventoryViewModel;
	}

	/** Rebuild the active layout from the current ViewModel snapshot. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|UI")
	virtual void RefreshFromViewModel();

	FText GetDisplayedStatus() const;

	/** Captures the drag revision/destination. Opening or cancelling sends no item command. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|UI")
	bool OpenSplitQuantitySelection(FGuid ItemId, int64 ExpectedRevision, int32 DestinationSlot);
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|UI")
	bool ConfirmSplitQuantitySelection(FGuid& OutRequestId);
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|UI")
	void CancelSplitQuantitySelection();
	UFUNCTION(BlueprintPure, Category = "RPG|Item|UI")
	bool IsSplitQuantitySelectionOpen() const { return bSplitQuantityOpen; }
	UFUNCTION(BlueprintPure, Category = "RPG|Item|UI")
	bool CanConfirmSplitQuantitySelection() const;

protected:
	/** Status/reservations changed, not the item snapshot; keep existing controls alive. */
	virtual void RefreshInteractionFromViewModel();
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeOnDeactivated() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FKey GetWindowDragMouseButton() const override;
	virtual TOptional<FSlateRect> GetWindowDragScreenBounds() const override;

	/** Available to Blueprint without bypassing the command ViewModel. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Item|UI")
	TObjectPtr<URPGInventoryProjectionViewModel> InventoryViewModel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> LoadStateText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InventoryItemsText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EquipmentItemsText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> InventoryGrid;

	/** Authored only in the bag window; separate equipment/mail layouts need no counter. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InventoryCapacityText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> EquipmentLeftSlots;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> EquipmentRightSlots;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> InventoryTabButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> EquipmentTabButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseWindowButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CommandResultText;

private:
	friend struct FRPGInventorySplitQuantityTestFixture;
	bool EnsureSplitQuantityControls();
	bool IsSplitSnapshotValid() const;
	bool TryReadSplitQuantity(int32& OutQuantity) const;
	void RefreshSplitQuantitySelection();
	UFUNCTION()
	void HandleSplitQuantityChanged(const FText& Text);
	UFUNCTION()
	void HandleSplitQuantityCommitted(const FText& Text, ETextCommit::Type Method);
	UFUNCTION()
	void HandleConfirmSplitQuantity();
	UFUNCTION()
	void HandleCancelSplitQuantity();
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> SplitQuantityPanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SplitQuantityDescription;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> SplitQuantityInput;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ConfirmSplitQuantityButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelSplitQuantityButton;
	FGuid SplitSourceId;
	int64 SplitExpectedRevision = 0;
	int32 SplitDestinationSlot = INDEX_NONE;
	int32 SplitSourceQuantity = 0;
	bool bSplitQuantityOpen = false;

	void BindViewModel();
	void UnbindViewModel();
	void RefreshInventoryCapacity();
	void RefreshStatusFromViewModel();
	void HandleViewModelFieldChanged(
		UObject* Object,
		UE::FieldNotification::FFieldId FieldId);

	UFUNCTION()
	void HandleInventoryTabClicked();

	UFUNCTION()
	void HandleEquipmentTabClicked();

	UFUNCTION()
	void HandleMailTabClicked();

	UFUNCTION()
	void HandleRetryLoadClicked();
	UFUNCTION()
	void HandleInventoryRecoveryClicked();
	UPROPERTY(Transient)
	TObjectPtr<UButton> InventoryRecoveryButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> MailTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RetryLoadButton;

	bool bViewModelBound = false;
	bool bOwnsInventoryViewModel = false;
};
