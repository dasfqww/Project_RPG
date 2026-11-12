#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Auction/RPGAuctionCoordinator.h"
#include "RPGAuctionWidget.generated.h"

class UButton;
class UTextBlock;

/** Read/recovery view only. Closing never cancels or replaces the durable owner request. */
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
    void Subscribe();
    void RefreshView();
    TSharedPtr<FRPGAuctionCoordinator> ActiveOwner();
    UFUNCTION() void ReadPublic();
    UFUNCTION() void ReadMine();
    UFUNCTION() void NextPage();
    UFUNCTION() void SelectRow(FString Option,ESelectInfo::Type Type);
    UFUNCTION() void ReadDetail();
    UFUNCTION() void Recover();
    UFUNCTION() void Retry();
    UFUNCTION() void CheckStorage();
    UFUNCTION() void ReloadAssets();
    UFUNCTION() void Close();
    TWeakPtr<FRPGAuctionCoordinator> Owner;
    FRPGTradeIntentScope BoundContext;
    FDelegateHandle ChangeHandle;
    FGuid DisplayedPage, Selected;
    TArray<FGuid> RowIds;
    bool bUpdating=false,bClosing=false;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AssetText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RetryHelpText;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> Rows;
    UPROPERTY(Transient) TObjectPtr<UButton> PublicButton;
    UPROPERTY(Transient) TObjectPtr<UButton> MineButton;
    UPROPERTY(Transient) TObjectPtr<UButton> NextButton;
    UPROPERTY(Transient) TObjectPtr<UButton> DetailButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RecoverButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> StorageButton;
    UPROPERTY(Transient) TObjectPtr<UButton> AssetsButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
};
