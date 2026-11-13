#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Auction/RPGAuctionCoordinator.h"
#include "RPGAuctionWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UEditableTextBox;

/** Explicit listing consent and durable recovery. Closing never cancels a submitted request. */
UCLASS()
class PROJECT_RPG_API URPGAuctionWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetOwner(TSharedRef<FRPGAuctionCoordinator> InOwner);
    void CloseScreen();
    FSimpleMulticastDelegate OnClosed;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry&,const FKeyEvent&) override;
private:
    friend struct FRPGAuctionWidgetTestAccess;
    friend struct FRPGAuctionRegistrationWidgetTestAccess;
    void Subscribe();
    void RefreshView();
    TSharedPtr<FRPGAuctionCoordinator> ActiveOwner();
    UFUNCTION() void ReadPublic();
    UFUNCTION() void ReadMine();
    UFUNCTION() void NextPage();
    UFUNCTION() void SelectRow(FString Option,ESelectInfo::Type Type);
    UFUNCTION() void ReadDetail();
    UFUNCTION() void ReviewPurchase();
    UFUNCTION() void ReviewCancel();
    UFUNCTION() void ReadPayable();
    UFUNCTION() void ReviewClaim();
    UFUNCTION() void ReadPolicy();
    UFUNCTION() void ReloadBag();
    UFUNCTION() void SelectBag(FString Option,ESelectInfo::Type Type);
    UFUNCTION() void RegistrationInputChanged(const FText& Value);
    UFUNCTION() void ReviewRegistration();
    UFUNCTION() void Confirm();
    UFUNCTION() void Dismiss();
    UFUNCTION() void Recover();
    UFUNCTION() void Retry();
    UFUNCTION() void CheckStorage();
    UFUNCTION() void ReloadAssets();
    UFUNCTION() void Close();
    TWeakPtr<FRPGAuctionCoordinator> Owner;
    FRPGTradeIntentScope BoundContext;
    FDelegateHandle ChangeHandle;
    FGuid DisplayedPage, Selected, DisplayedReview;
    TArray<FGuid> RowIds;
    FGuid DisplayedBag,SelectedBag;
    TArray<FGuid> BagIds;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> RegistrationPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PolicyText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RegistrationStatus;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> BagRows;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> QuantityInput;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> PriceInput;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> DurationInput;
    UPROPERTY(Transient) TObjectPtr<UButton> PolicyButton;
    UPROPERTY(Transient) TObjectPtr<UButton> BagButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RegisterButton;
    bool bUpdating=false,bClosing=false;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AssetText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RetryHelpText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReviewText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PayableText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ClaimResultText;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> Rows;
    UPROPERTY(Transient) TObjectPtr<UButton> PublicButton;
    UPROPERTY(Transient) TObjectPtr<UButton> MineButton;
    UPROPERTY(Transient) TObjectPtr<UButton> NextButton;
    UPROPERTY(Transient) TObjectPtr<UButton> DetailButton;
    UPROPERTY(Transient) TObjectPtr<UButton> PurchaseButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CancelSaleButton;
    UPROPERTY(Transient) TObjectPtr<UButton> PayableButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ClaimButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
    UPROPERTY(Transient) TObjectPtr<UButton> DismissButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RecoverButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> StorageButton;
    UPROPERTY(Transient) TObjectPtr<UButton> AssetsButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
};
