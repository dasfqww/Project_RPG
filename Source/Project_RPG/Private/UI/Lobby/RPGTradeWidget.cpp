#include "UI/Lobby/RPGTradeWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "InputCoreTypes.h"

TSharedRef<SWidget> URPGTradeWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("TradeTree"));
    if (!WidgetTree->RootWidget)
    {
        auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TradeRoot"));
        WidgetTree->RootWidget = Root;
        auto* Shade = WidgetTree->ConstructWidget<UBorder>(); Shade->SetBrushColor(FLinearColor(0, 0, 0, .75f));
        auto* ShadeSlot = Root->AddChildToCanvas(Shade); ShadeSlot->SetAnchors(FAnchors(0, 0, 1, 1)); ShadeSlot->SetOffsets(FMargin(0));
        auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(640);
        auto* DialogSlot = Root->AddChildToCanvas(Size); DialogSlot->SetAnchors(FAnchors(.5f)); DialogSlot->SetAlignment(FVector2D(.5f)); DialogSlot->SetAutoSize(true);
        auto* Panel = WidgetTree->ConstructWidget<UBorder>(); Panel->SetPadding(FMargin(24)); Panel->SetBrushColor(FLinearColor(.035f, .05f, .07f, 1)); Size->AddChild(Panel);
        auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>(); Panel->SetContent(Rows);
        auto Text = [&](const TCHAR* Name, const FText& Label)
        { auto* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name)); T->SetText(Label); T->SetAutoWrapText(true); Rows->AddChild(T); return T; };
        Text(TEXT("TradeTitle"), NSLOCTEXT("RPGTrade", "Title", "거래 내역"));
        StatusText = Text(TEXT("TradeStatus"), FText::GetEmpty());
        Text(TEXT("TradeMyLabel"), NSLOCTEXT("RPGTrade", "Mine", "내 제안 요약")); MyOfferText = Text(TEXT("TradeMyOffer"), FText::GetEmpty());
        Text(TEXT("TradeOtherLabel"), NSLOCTEXT("RPGTrade", "Other", "상대 제안 요약")); OtherOfferText = Text(TEXT("TradeOtherOffer"), FText::GetEmpty());
        auto Button = [&](const TCHAR* Name, const FText& Label)
        { auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name)); auto* T = WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(Label); B->SetContent(T); Rows->AddChild(B); return B; };
        RefreshButton = Button(TEXT("TradeRefresh"), NSLOCTEXT("RPGTrade", "Refresh", "새로 고침"));
        RetryButton = Button(TEXT("TradeRetry"), NSLOCTEXT("RPGTrade", "Retry", "처리 결과 다시 확인"));
        AcceptButton = Button(TEXT("TradeAccept"), NSLOCTEXT("RPGTrade", "Accept", "초대 수락"));
        WithdrawButton = Button(TEXT("TradeWithdraw"), NSLOCTEXT("RPGTrade", "Withdraw", "준비 해제"));
        CancelButton = Button(TEXT("TradeCancel"), NSLOCTEXT("RPGTrade", "Cancel", "거래 취소"));
        CloseButton = Button(TEXT("TradeClose"), NSLOCTEXT("RPGTrade", "Close", "닫기"));
        RefreshButton->OnClicked.AddDynamic(this, &ThisClass::RefreshTrade); RetryButton->OnClicked.AddDynamic(this, &ThisClass::RetryTrade);
        AcceptButton->OnClicked.AddDynamic(this, &ThisClass::AcceptTrade); CancelButton->OnClicked.AddDynamic(this, &ThisClass::CancelTrade);
        WithdrawButton->OnClicked.AddDynamic(this, &ThisClass::WithdrawTrade); CloseButton->OnClicked.AddDynamic(this, &ThisClass::CloseTrade);
    }
    SetIsFocusable(true); RefreshView(); return Super::RebuildWidget();
}
void URPGTradeWidget::SetTradeOwner(TSharedRef<FRPGTradeLobbyOwner> Owner)
{
    if (auto Old = TradeOwner.Pin()) Old->OnChanged.Remove(ChangeHandle);
    TradeOwner = Owner; Subscribe(); RefreshView();
}
void URPGTradeWidget::Subscribe()
{
    if (auto Owner = TradeOwner.Pin()) { Owner->OnChanged.Remove(ChangeHandle); ChangeHandle = Owner->OnChanged.AddUObject(this, &ThisClass::RefreshView); }
}
void URPGTradeWidget::NativeConstruct() { Super::NativeConstruct(); Subscribe(); RefreshView(); if (CloseButton && GetOwningPlayer()) CloseButton->SetKeyboardFocus(); }
void URPGTradeWidget::NativeDestruct()
{
    if (auto Owner = TradeOwner.Pin()) Owner->OnChanged.Remove(ChangeHandle);
    ChangeHandle.Reset(); Super::NativeDestruct();
}
void URPGTradeWidget::RefreshView()
{
    const auto Owner = TradeOwner.Pin();
    const auto View = Owner ? Owner->GetView() : FRPGTradeLobbyView{};
    if (StatusText) StatusText->SetText(Owner ? View.Status : NSLOCTEXT("RPGTrade", "Unavailable", "거래 상태를 확인할 수 없습니다."));
    if (MyOfferText) MyOfferText->SetText(View.MyOffer); if (OtherOfferText) OtherOfferText->SetText(View.OtherOffer);
    if (RefreshButton) RefreshButton->SetIsEnabled(View.bRefresh); if (RetryButton) RetryButton->SetIsEnabled(View.bRetry);
    if (AcceptButton) AcceptButton->SetIsEnabled(View.bAccept); if (CancelButton) CancelButton->SetIsEnabled(View.bCancel);
    if (WithdrawButton) WithdrawButton->SetIsEnabled(View.bWithdraw);
}
void URPGTradeWidget::RefreshTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Refresh(); }
void URPGTradeWidget::RetryTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Retry(); }
void URPGTradeWidget::AcceptTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Submit(ERPGTradeAction::Accept); }
void URPGTradeWidget::CancelTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Submit(ERPGTradeAction::Cancel); }
void URPGTradeWidget::WithdrawTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Submit(ERPGTradeAction::Withdraw); }
void URPGTradeWidget::CloseScreen() { CloseTrade(); }
void URPGTradeWidget::CloseTrade()
{
    if (auto Owner = TradeOwner.Pin()) Owner->DismissClosed();
    if (IsInViewport() || GetParent()) RemoveFromParent();
    OnClosed.Broadcast();
}
FReply URPGTradeWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { if (!Event.IsRepeat()) CloseTrade(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry, Event);
}
