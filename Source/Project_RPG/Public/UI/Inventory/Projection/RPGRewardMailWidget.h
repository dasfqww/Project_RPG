#pragma once

#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "Component/RPGItemCommandComponent.h"
#include "RPGRewardMailWidget.generated.h"

class UVerticalBox;

/** WBP-editable attachment row; item commands remain in the shared ViewModel. */
UCLASS()
class PROJECT_RPG_API URPGRewardMailEntryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void InitializeEntry(const FRPGInventoryProjectionEntry& Entry,
		URPGInventoryProjectionViewModel* ViewModel);
	const FGuid& GetAttachmentId() const { return Attachment.GetItemId(); }
	/** Claim availability/feedback only; the attachment name remains on a separate line. */
	FText GetFeedbackText() const;
	/** Update label/enabled/reason only; preserve the row and its focus. */
	void RefreshClaimPresentation();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ClaimButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ClaimButtonText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MailItemName;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MailClaimFeedback;
private:
	UFUNCTION()
	void HandleClaimClicked();
	void RefreshAttachmentPresentation();

	UPROPERTY(Transient)
	TObjectPtr<URPGInventoryProjectionViewModel> EntryViewModel;
	FRPGInventoryProjectionEntry Attachment;
};

/** WBP-editable mailbox with a native layout fallback for missing presentation assets. */
UCLASS()
class PROJECT_RPG_API URPGRewardMailWidget : public URPGInventoryProjectionWidget
{
	GENERATED_BODY()
public:
	URPGRewardMailWidget();
	virtual void RefreshFromViewModel() override;
protected:
	virtual void RefreshInteractionFromViewModel() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Item|UI")
	TSubclassOf<URPGRewardMailEntryWidget> MailEntryWidgetClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> MailRows;
};
