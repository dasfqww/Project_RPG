#pragma once
#include "Blueprint/UserWidget.h"
#include "Trading/RPGTradeLobbyOwner.h"
#include "RPGTradeWidget.generated.h"

class UButton;
class UTextBlock;

/** Read/accept/cancel view. Asset consent controls require the subsequent exact-item preview flow. */
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
    void RefreshView();
    void Subscribe();
    UFUNCTION() void RefreshTrade();
    UFUNCTION() void RetryTrade();
    UFUNCTION() void AcceptTrade();
    UFUNCTION() void CancelTrade();
    UFUNCTION() void WithdrawTrade();
    UFUNCTION() void CloseTrade();
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
};
