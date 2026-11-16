#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Shop/RPGShopCoordinator.h"
#include "Types/SlateEnums.h"
#include "RPGShopWidget.generated.h"

class UBorder;
class UCheckBox;
class UEditableTextBox;
class UHorizontalBox;
class UImage;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class FRPGWindowInputState;

/** A selectable row. Mouse buttons never directly buy, sell or use an item. */
UCLASS()
class PROJECT_RPG_API URPGShopRowButton final : public UButton
{
    GENERATED_BODY()
public:
    void BindChoice(FSimpleDelegate InChoice);
private:
    UFUNCTION() void Choose();
    FSimpleDelegate Choice;
};

/** Shop presentation only; the coordinator owns requests, consent and recovery. */
UCLASS()
class PROJECT_RPG_API URPGShopWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetShopOwner(TSharedRef<FRPGShopCoordinator> InOwner);
    UFUNCTION()
    void CloseScreen();
    FSimpleMulticastDelegate OnClosed;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry&, float) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
private:
    friend struct FRPGShopWidgetTestAccess;
    void Subscribe();
    void RefreshView();
    void RebuildRows(const FRPGShopScreenView& View);
    void RefreshDetails(const FRPGShopScreenView& View);
    void UpdateLayout(FVector2D Viewport);
    void UpdateNavigation();
    void SelectOffer(FGuid Snapshot, FString Id);
    void SelectItem(FGuid Snapshot, FGuid Item);
    void ClearSelection();
    void FocusSelectedRow();
    void SetQuantity(int32 Quantity);
    bool ReadQuantity(int32& Out) const;
    int32 MaximumQuantity(const FRPGShopScreenView& View) const;
    void ReleaseInput();
    UButton* MakeButton(const TCHAR* Name, const FText& Label);
    UTextBlock* MakeText(const FText& Label, int32 Size = 14);
    void StyleButton(UButton& Button) const;
    static void LabelButton(UButton* Button, const FText& Label);

    UFUNCTION() void BuyTab();
    UFUNCTION() void SellTab();
    UFUNCTION() void ShowUnavailable(bool bShow);
    UFUNCTION() void Reload();
    UFUNCTION() void QuantityEdited(const FText& Value);
    UFUNCTION() void QuantityCommitted(const FText& Value, ETextCommit::Type Method);
    UFUNCTION() void Decrease();
    UFUNCTION() void Increase();
    UFUNCTION() void Maximum();
    UFUNCTION() void Execute();
    UFUNCTION() void ConfirmSale();
    UFUNCTION() void DismissSale();
    UFUNCTION() void BackToList();
    UFUNCTION() void Recover();
    UFUNCTION() void Retry();
    UFUNCTION() void ReloadAssets();
    UFUNCTION() void RecheckStorage();

    TWeakPtr<FRPGShopCoordinator> Owner;
    TSharedPtr<FRPGWindowInputState> InputState;
    FDelegateHandle ChangedHandle;
    FRPGTradeIntentScope BoundContext;
    FGuid DisplayedSnapshot, SelectedItem, DisplayedReview;
    FString SelectedOffer;
    TArray<FString> RowKeys;
    bool bSell = false, bShowUnavailable = false, bUpdating = false, bClosing = false;
    bool bNarrow = false, bDetailPage = false, bRowsDirty = true;
    float ViewRefreshElapsed = 0;
    FText InputError;

    UPROPERTY(Transient) TObjectPtr<USizeBox> WindowBounds;
    UPROPERTY(Transient) TObjectPtr<UBorder> WindowBorder;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> ListPanel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> DetailPanel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> ItemRows;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> ListScroll;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> DetailScroll;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WalletText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CapacityText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ModeText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> EmptyText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> QuantityHint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TotalText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PendingText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReviewText;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> UnavailableCheck;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> QuantityInput;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> QuantityRow;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> ReviewActions;
    UPROPERTY(Transient) TObjectPtr<UButton> BuyButton;
    UPROPERTY(Transient) TObjectPtr<UButton> SellButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ReloadButton;
    UPROPERTY(Transient) TObjectPtr<UButton> MinusButton;
    UPROPERTY(Transient) TObjectPtr<UButton> PlusButton;
    UPROPERTY(Transient) TObjectPtr<UButton> MaximumButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ExecuteButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
    UPROPERTY(Transient) TObjectPtr<UButton> DismissButton;
    UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RecoverButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> AssetsButton;
    UPROPERTY(Transient) TObjectPtr<UButton> StorageButton;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> BackgroundTexture;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> FrameTexture;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> ButtonTexture;
};
