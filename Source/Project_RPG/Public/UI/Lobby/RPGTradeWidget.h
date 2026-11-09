#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Trading/RPGTradeLobbyOwner.h"
#include "RPGTradeWidget.generated.h"

class UButton;
class UTextBlock;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UVerticalBox;

/** Explicit discovery/invitation and exact-offer review. The local owner outlives the view. */
UCLASS()
class PROJECT_RPG_API URPGTradeWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetTradeOwner(TSharedRef<FRPGTradeLobbyOwner> Owner);
    void CloseScreen();
    FSimpleMulticastDelegate OnClosed;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
private:
    friend struct FRPGTradeWidgetTestAccess;
    friend struct FRPGTradeOfferWidgetAccess;
    void RefreshView();
    void Subscribe();
    UFUNCTION() void RefreshTrade();
    UFUNCTION() void RetryTrade();
    UFUNCTION() void AcceptTrade();
    UFUNCTION() void CancelTrade();
    UFUNCTION() void WithdrawTrade();
    UFUNCTION() void CloseTrade();
    UFUNCTION() void ReviewChanged(bool bChecked);
    UFUNCTION() void ReadyTrade();
    UFUNCTION() void ConfirmTrade();
    UFUNCTION() void ReloadBag();
    UFUNCTION() void BeginOfferEdit();
    UFUNCTION() void DiscardOfferEdit();
    UFUNCTION() void SendOfferEdit();
    UFUNCTION() void AmountEdited(const FText& Text);
    UFUNCTION() void QuantityEdited(const FText& Text);
    UFUNCTION() void AddOfferItem();
    UFUNCTION() void RemoveOfferItem();
    UFUNCTION() void SelectBagItem(FString Option, ESelectInfo::Type Type);
    UFUNCTION() void SelectOfferItem(FString Option, ESelectInfo::Type Type);
    void RefreshDraft();
    UFUNCTION() void TargetNameEdited(const FText& Text);
    UFUNCTION() void FindTarget();
    UFUNCTION() void SelectTarget(FString Option, ESelectInfo::Type Type);
    UFUNCTION() void InviteSelectedTarget();
    UFUNCTION() void ReloadCompletedAssets();
    void RefreshTargets();
    TWeakPtr<FRPGTradeLobbyOwner> TradeOwner;
    FDelegateHandle ChangeHandle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MyOfferText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OtherOfferText;
    UPROPERTY(Transient) TObjectPtr<UButton> RefreshButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> AcceptButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CancelButton;
    UPROPERTY(Transient) TObjectPtr<UButton> WithdrawButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MyDetailsText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OtherDetailsText;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> ReviewCheck;
    UPROPERTY(Transient) TObjectPtr<UButton> ReadyButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
    FGuid DisplayedReviewToken, ReviewedToken;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DraftStatusText;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> DraftPanel;
    UPROPERTY(Transient) TObjectPtr<UButton> ReloadBagButton;
    UPROPERTY(Transient) TObjectPtr<UButton> BeginDraftButton;
    UPROPERTY(Transient) TObjectPtr<UButton> DiscardDraftButton;
    UPROPERTY(Transient) TObjectPtr<UButton> SendDraftButton;
    UPROPERTY(Transient) TObjectPtr<UButton> AddDraftButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RemoveDraftButton;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> BagBox;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> OfferBox;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> AmountBox;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> QuantityBox;
    TArray<FString> BagLabels, OfferLabels;
    TArray<FGuid> BagIds, OfferIds;
    FGuid DisplayedDraftToken, DisplayedBagToken, SelectedBagItem, SelectedOfferItem;
    bool bUpdatingDraft = false;
    FText DraftInputError;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> TargetPanel;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> TargetNameBox;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TargetStatusText;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> TargetBox;
    UPROPERTY(Transient) TObjectPtr<UButton> SearchTargetButton;
    UPROPERTY(Transient) TObjectPtr<UButton> InviteTargetButton;
    FGuid DisplayedTargetInput, DisplayedTargetResult, SelectedTarget;
    TArray<FString> TargetLabels;
    TArray<FGuid> TargetIds;
    bool bUpdatingTargets=false;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AssetStatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AssetSummaryText;
    UPROPERTY(Transient) TObjectPtr<UButton> ReloadAssetsButton;
};
