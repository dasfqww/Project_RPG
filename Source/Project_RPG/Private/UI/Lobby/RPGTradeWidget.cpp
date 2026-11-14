#include "UI/Lobby/RPGTradeWidget.h"
#include "UI/Lobby/RPGTradingWindowLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
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
        const auto Frame=FRPGTradingWindowLayout::Build(*WidgetTree,TEXT("Trade"),FVector2D(640,680));
        WindowBounds=Frame.Bounds;WindowBorder=Frame.Border;
        auto* Rows=Frame.Header;
        auto Text = [&](const TCHAR* Name, const FText& Label)
        { auto* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name)); T->SetText(Label); FRPGTradingWindowLayout::FormatText(*T); Rows->AddChild(T); return T; };
        FRPGTradingWindowLayout::FormatText(*Text(TEXT("TradeTitle"), NSLOCTEXT("RPGTrade", "Title", "거래 내역")),true);
        Rows=Frame.Content;
        StatusText = Text(TEXT("TradeStatus"), FText::GetEmpty());
        Text(TEXT("TradeMyLabel"), NSLOCTEXT("RPGTrade", "Mine", "내 제안 요약")); MyOfferText = Text(TEXT("TradeMyOffer"), FText::GetEmpty());
        MyDetailsText = Text(TEXT("TradeMyDetails"), FText::GetEmpty());
        Text(TEXT("TradeOtherLabel"), NSLOCTEXT("RPGTrade", "Other", "상대 제안 요약")); OtherOfferText = Text(TEXT("TradeOtherOffer"), FText::GetEmpty());
        OtherDetailsText = Text(TEXT("TradeOtherDetails"), FText::GetEmpty());
        auto Button = [&](const TCHAR* Name, const FText& Label)
        { auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name)); auto* T = WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(Label); FRPGTradingWindowLayout::FormatText(*T); B->SetContent(T); Rows->AddChild(B); return B; };
        RefreshButton = Button(TEXT("TradeRefresh"), NSLOCTEXT("RPGTrade", "Refresh", "새로 고침"));
        RetryButton = Button(TEXT("TradeRetry"), NSLOCTEXT("RPGTrade", "Retry", "처리 결과 다시 확인"));
        AssetStatusText=Text(TEXT("TradeAssetStatus"),FText::GetEmpty());AssetSummaryText=Text(TEXT("TradeAssetSummary"),FText::GetEmpty());
        ReloadAssetsButton=Button(TEXT("TradeAssetsReload"),NSLOCTEXT("RPGTrade","ReloadAssets","완료 후 자산 다시 불러오기"));
        ReloadAssetsButton->OnClicked.AddDynamic(this,&ThisClass::ReloadCompletedAssets);
        auto* OuterRows=Rows; TargetPanel=WidgetTree->ConstructWidget<UVerticalBox>();Rows->AddChild(TargetPanel);Rows=TargetPanel;
        Text(TEXT("TradeTargetTitle"),NSLOCTEXT("RPGTrade","TargetTitle","거래 상대 찾기"));
        TargetNameBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("TradeTargetName"));
        TargetNameBox->SetHintText(NSLOCTEXT("RPGTrade","TargetInput","상대 캐릭터의 정확한 이름"));Rows->AddChild(TargetNameBox);
        SearchTargetButton=Button(TEXT("TradeTargetSearch"),NSLOCTEXT("RPGTrade","TargetSearch","이름으로 검색"));
        TargetStatusText=Text(TEXT("TradeTargetStatus"),FText::GetEmpty());
        TargetBox=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("TradeTargetChoices"));Rows->AddChild(TargetBox);
        InviteTargetButton=Button(TEXT("TradeTargetInvite"),NSLOCTEXT("RPGTrade","TargetInvite","선택한 상대에게 초대 보내기"));
        Rows=OuterRows;
        AcceptButton = Button(TEXT("TradeAccept"), NSLOCTEXT("RPGTrade", "Accept", "초대 수락"));
        DraftPanel=WidgetTree->ConstructWidget<UVerticalBox>(); Rows->AddChild(DraftPanel);
        auto* MainRows=Rows; Rows=DraftPanel;
        Text(TEXT("TradeDraftTitle"),NSLOCTEXT("RPGTrade","DraftTitle","내 제안 편집"));
        DraftStatusText=Text(TEXT("TradeDraftStatus"),FText::GetEmpty());
        ReloadBagButton=Button(TEXT("TradeReloadBag"),NSLOCTEXT("RPGTrade","ReloadBag","최신 가방 불러오기"));
        BeginDraftButton=Button(TEXT("TradeBeginDraft"),NSLOCTEXT("RPGTrade","BeginOfferEdit","현재 내 제안 불러오기"));
        BagBox=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("TradeBagItems")); Rows->AddChild(BagBox);
        QuantityBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("TradeDraftQuantity"));QuantityBox->SetText(FText::FromString(TEXT("1")));QuantityBox->SetHintText(NSLOCTEXT("RPGTrade","Quantity","물품 수량"));Rows->AddChild(QuantityBox);
        AddDraftButton=Button(TEXT("TradeAddItem"),NSLOCTEXT("RPGTrade","AddOfferItem","물품 추가 · 수량 변경"));
        OfferBox=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("TradeDraftItems"));Rows->AddChild(OfferBox);
        RemoveDraftButton=Button(TEXT("TradeRemoveItem"),NSLOCTEXT("RPGTrade","RemoveOfferItem","선택 물품 삭제"));
        Text(TEXT("TradeDraftGoldLabel"),NSLOCTEXT("RPGTrade","DraftGold","제안 골드"));
        AmountBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("TradeDraftAmount"));AmountBox->SetHintText(NSLOCTEXT("RPGTrade","Amount","0 이상의 정수"));Rows->AddChild(AmountBox);
        SendDraftButton=Button(TEXT("TradeSendOffer"),NSLOCTEXT("RPGTrade","SendOfferEdit","수정한 제안 보내기"));
        DiscardDraftButton=Button(TEXT("TradeDiscardOffer"),NSLOCTEXT("RPGTrade","DiscardOfferEdit","편집 취소"));
        Rows=MainRows;
        ReviewCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("TradeReview"));
        auto* ReviewLabel = WidgetTree->ConstructWidget<UTextBlock>(); ReviewLabel->SetText(NSLOCTEXT("RPGTrade", "Review", "양쪽 제안과 물품 상세를 확인했습니다.")); ReviewLabel->SetAutoWrapText(true);
        ReviewCheck->SetContent(ReviewLabel); Rows->AddChild(ReviewCheck);
        ReadyButton = Button(TEXT("TradeReady"), NSLOCTEXT("RPGTrade", "Ready", "확인한 제안으로 준비"));
        ConfirmButton = Button(TEXT("TradeConfirm"), NSLOCTEXT("RPGTrade", "Confirm", "거래 최종 확정"));
        WithdrawButton = Button(TEXT("TradeWithdraw"), NSLOCTEXT("RPGTrade", "Withdraw", "준비 해제"));
        CancelButton = Button(TEXT("TradeCancel"), NSLOCTEXT("RPGTrade", "Cancel", "거래 취소"));
        Rows=Frame.Footer;
        CloseButton = Button(TEXT("TradeClose"), NSLOCTEXT("RPGTrade", "Close", "닫기"));
        RefreshButton->OnClicked.AddDynamic(this, &ThisClass::RefreshTrade); RetryButton->OnClicked.AddDynamic(this, &ThisClass::RetryTrade);
        AcceptButton->OnClicked.AddDynamic(this, &ThisClass::AcceptTrade); CancelButton->OnClicked.AddDynamic(this, &ThisClass::CancelTrade);
        WithdrawButton->OnClicked.AddDynamic(this, &ThisClass::WithdrawTrade); CloseButton->OnClicked.AddDynamic(this, &ThisClass::CloseTrade);
        ReviewCheck->OnCheckStateChanged.AddDynamic(this,&ThisClass::ReviewChanged);
        ReadyButton->OnClicked.AddDynamic(this,&ThisClass::ReadyTrade); ConfirmButton->OnClicked.AddDynamic(this,&ThisClass::ConfirmTrade);
        ReloadBagButton->OnClicked.AddDynamic(this,&ThisClass::ReloadBag);BeginDraftButton->OnClicked.AddDynamic(this,&ThisClass::BeginOfferEdit);
        AddDraftButton->OnClicked.AddDynamic(this,&ThisClass::AddOfferItem);RemoveDraftButton->OnClicked.AddDynamic(this,&ThisClass::RemoveOfferItem);
        SendDraftButton->OnClicked.AddDynamic(this,&ThisClass::SendOfferEdit);DiscardDraftButton->OnClicked.AddDynamic(this,&ThisClass::DiscardOfferEdit);
        AmountBox->OnTextChanged.AddDynamic(this,&ThisClass::AmountEdited);QuantityBox->OnTextChanged.AddDynamic(this,&ThisClass::QuantityEdited);
        BagBox->OnSelectionChanged.AddDynamic(this,&ThisClass::SelectBagItem);OfferBox->OnSelectionChanged.AddDynamic(this,&ThisClass::SelectOfferItem);
        TargetNameBox->OnTextChanged.AddDynamic(this,&ThisClass::TargetNameEdited);
        SearchTargetButton->OnClicked.AddDynamic(this,&ThisClass::FindTarget);InviteTargetButton->OnClicked.AddDynamic(this,&ThisClass::InviteSelectedTarget);
        TargetBox->OnSelectionChanged.AddDynamic(this,&ThisClass::SelectTarget);
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
void URPGTradeWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{Super::NativeTick(Geometry,DeltaSeconds);FRPGTradingWindowLayout::Apply(WindowBounds,WindowBorder,Geometry.GetLocalSize(),FVector2D(640,680));}
void URPGTradeWidget::NativeDestruct()
{
    if (auto Owner = TradeOwner.Pin()) Owner->OnChanged.Remove(ChangeHandle);
    ChangeHandle.Reset(); Super::NativeDestruct();
    ReviewedToken.Invalidate(); DisplayedReviewToken.Invalidate();
    SelectedTarget.Invalidate(); DisplayedTargetResult.Invalidate();
}
void URPGTradeWidget::RefreshView()
{
    const auto Owner = TradeOwner.Pin();
    const auto View = Owner ? Owner->GetView() : FRPGTradeLobbyView{};
    if (DisplayedReviewToken != View.ReviewToken)
    {
        DisplayedReviewToken = View.ReviewToken; ReviewedToken.Invalidate();
        if (ReviewCheck) ReviewCheck->SetIsChecked(false);
    }
    if (StatusText) StatusText->SetText(Owner ? View.Status : NSLOCTEXT("RPGTrade", "Unavailable", "거래 상태를 확인할 수 없습니다."));
    if(AssetStatusText)AssetStatusText->SetText(View.AssetStatus);
    if(AssetSummaryText)AssetSummaryText->SetText(View.AssetSummary);
    if(ReloadAssetsButton){ReloadAssetsButton->SetVisibility(View.AssetStatus.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::Visible);ReloadAssetsButton->SetIsEnabled(View.bReloadAssets);}
    if (MyOfferText) MyOfferText->SetText(View.MyOffer); if (OtherOfferText) OtherOfferText->SetText(View.OtherOffer);
    if (RefreshButton) RefreshButton->SetIsEnabled(View.bRefresh); if (RetryButton) RetryButton->SetIsEnabled(View.bRetry);
    if (AcceptButton) AcceptButton->SetIsEnabled(View.bAccept); if (CancelButton) CancelButton->SetIsEnabled(View.bCancel);
    if (WithdrawButton) WithdrawButton->SetIsEnabled(View.bWithdraw);
    if (MyDetailsText) MyDetailsText->SetText(View.MyDetails); if (OtherDetailsText) OtherDetailsText->SetText(View.OtherDetails);
    if (ReviewCheck) ReviewCheck->SetIsEnabled(View.bCanReview);
    const bool Reviewed = View.ReviewToken.IsValid() && ReviewedToken == View.ReviewToken && ReviewCheck && ReviewCheck->IsChecked();
    if (ReadyButton) ReadyButton->SetIsEnabled(View.bReady && Reviewed); if (ConfirmButton) ConfirmButton->SetIsEnabled(View.bConfirm && Reviewed);
    RefreshDraft();
    RefreshTargets();
}
void URPGTradeWidget::RefreshTargets()
{
    if (bUpdatingTargets || !TargetPanel) return;
    TGuardValue<bool> Guard(bUpdatingTargets,true);
    const auto Owner=TradeOwner.Pin(); const auto View=Owner ? Owner->GetTargetView() : FRPGTradeTargetView{};
    TargetPanel->SetVisibility(View.bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (DisplayedTargetInput!=View.InputToken)
    { DisplayedTargetInput=View.InputToken; TargetNameBox->SetText(FText::FromString(View.Query)); }
    if (DisplayedTargetResult!=View.ResultToken)
    {
        DisplayedTargetResult=View.ResultToken; SelectedTarget.Invalidate(); TargetLabels.Reset(); TargetIds.Reset(); TargetBox->ClearOptions();
        for (const auto& Target:View.Targets) { TargetLabels.Add(Target.Name);TargetIds.Add(Target.Character);TargetBox->AddOption(Target.Name); }
    }
    TargetNameBox->SetIsEnabled(View.bCanEdit); SearchTargetButton->SetIsEnabled(View.bCanSearch);
    TargetStatusText->SetText(View.Status); TargetBox->SetIsEnabled(View.bCanInvite);
    InviteTargetButton->SetIsEnabled(View.bCanInvite && TargetIds.Contains(SelectedTarget));
}
void URPGTradeWidget::TargetNameEdited(const FText& Text)
{ if (!bUpdatingTargets) if (auto O=TradeOwner.Pin()) O->SetTargetName(DisplayedTargetInput,Text.ToString()); }
void URPGTradeWidget::FindTarget() { if(auto O=TradeOwner.Pin()) O->SearchTargets(DisplayedTargetInput); RefreshTargets(); }
void URPGTradeWidget::SelectTarget(FString Option,ESelectInfo::Type)
{ if(bUpdatingTargets)return;const int32 I=TargetLabels.IndexOfByKey(Option);SelectedTarget=TargetIds.IsValidIndex(I)?TargetIds[I]:FGuid();RefreshTargets(); }
void URPGTradeWidget::InviteSelectedTarget()
{ if(auto O=TradeOwner.Pin()) O->InviteTarget(DisplayedTargetResult,SelectedTarget);RefreshView(); }
void URPGTradeWidget::ReloadCompletedAssets(){if(auto O=TradeOwner.Pin())O->ReloadAssets();RefreshView();}
void URPGTradeWidget::RefreshDraft()
{
    if (bUpdatingDraft || !DraftStatusText) return;
    TGuardValue<bool> Guard(bUpdatingDraft,true);
    const auto Owner=TradeOwner.Pin();const auto View=Owner ? Owner->GetDraftView() : FRPGTradeDraftView{};
    const auto* Session=Owner ? Owner->GetClient().GetSession() : nullptr;
    if (DraftPanel) DraftPanel->SetVisibility(View.Token.IsValid() || (Session && Session->Status==ERPGTradeStatus::Negotiating) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (DisplayedDraftToken!=View.Token)
    {
        DisplayedDraftToken=View.Token;DraftInputError=FText::GetEmpty();SelectedOfferItem.Invalidate();
        AmountBox->SetText(FText::FromString(View.Token.IsValid() ? View.AmountText : TEXT("0")));QuantityBox->SetText(FText::FromString(TEXT("1")));
    }
    TArray<FString> NewBagLabels,NewOfferLabels;TArray<FGuid> NewBagIds,NewOfferIds;
    for (const auto& Item : View.BagItems) if (Item.bEligible && !Item.Label.IsEmpty())
    {NewBagIds.Add(Item.ItemId);NewBagLabels.Add(FText::Format(NSLOCTEXT("RPGTrade","BagChoice","{0}. {1} · {2}개"),FText::AsNumber(NewBagIds.Num()),Item.Label,FText::AsNumber(Item.Quantity)).ToString());}
    for (const auto& Item : View.SelectedItems)
    {NewOfferIds.Add(Item.ItemId);NewOfferLabels.Add(FText::Format(NSLOCTEXT("RPGTrade","OfferChoice","{0}. {1} × {2}"),FText::AsNumber(NewOfferIds.Num()),Item.Label,FText::AsNumber(Item.Quantity)).ToString());}
    if (DisplayedBagToken!=View.BagToken || BagLabels!=NewBagLabels || BagIds!=NewBagIds)
    {
        if (DisplayedBagToken!=View.BagToken) SelectedBagItem.Invalidate();
        DisplayedBagToken=View.BagToken;BagLabels=MoveTemp(NewBagLabels);BagIds=MoveTemp(NewBagIds);BagBox->ClearOptions();for(const auto& Label:BagLabels)BagBox->AddOption(Label);
        const int32 I=BagIds.IndexOfByKey(SelectedBagItem);if(I!=INDEX_NONE)BagBox->SetSelectedOption(BagLabels[I]);else SelectedBagItem.Invalidate();
    }
    if (OfferLabels!=NewOfferLabels || OfferIds!=NewOfferIds)
    {
        OfferLabels=MoveTemp(NewOfferLabels);OfferIds=MoveTemp(NewOfferIds);OfferBox->ClearOptions();for(const auto& Label:OfferLabels)OfferBox->AddOption(Label);
        const int32 I=OfferIds.IndexOfByKey(SelectedOfferItem);if(I!=INDEX_NONE)OfferBox->SetSelectedOption(OfferLabels[I]);else SelectedOfferItem.Invalidate();
    }
    DraftStatusText->SetText(DraftInputError.IsEmpty() ? View.Status : DraftInputError);
    ReloadBagButton->SetIsEnabled(View.bCanReload);BeginDraftButton->SetIsEnabled(View.bCanBegin);SendDraftButton->SetIsEnabled(View.bCanSend);DiscardDraftButton->SetIsEnabled(View.Token.IsValid());
    BagBox->SetIsEnabled(View.bCanEdit && !BagIds.IsEmpty());OfferBox->SetIsEnabled(View.bCanEdit && !OfferIds.IsEmpty());AmountBox->SetIsEnabled(View.bCanEdit);QuantityBox->SetIsEnabled(View.bCanEdit);
    int64 Quantity=0;const auto* Choice=View.BagItems.FindByPredicate([&](const FRPGTradeBagItem& I){return I.ItemId==SelectedBagItem;});
    AddDraftButton->SetIsEnabled(View.bCanEdit && Choice && Choice->bEligible && FRPGTradeOfferDraft::ParseInteger(QuantityBox->GetText().ToString(),Quantity) && Quantity>0 && Quantity<=Choice->Quantity);
    RemoveDraftButton->SetIsEnabled(View.bCanEdit && OfferIds.Contains(SelectedOfferItem));
}
void URPGTradeWidget::ReloadBag(){DraftInputError=FText::GetEmpty();if(auto O=TradeOwner.Pin())O->ReloadBag();RefreshDraft();}
void URPGTradeWidget::BeginOfferEdit(){DraftInputError=FText::GetEmpty();if(auto O=TradeOwner.Pin())O->BeginDraft();RefreshView();}
void URPGTradeWidget::DiscardOfferEdit(){DraftInputError=FText::GetEmpty();if(auto O=TradeOwner.Pin())O->DiscardDraft();RefreshView();}
void URPGTradeWidget::SendOfferEdit(){DraftInputError=FText::GetEmpty();if(auto O=TradeOwner.Pin())O->SendDraft(DisplayedDraftToken);RefreshView();}
void URPGTradeWidget::AmountEdited(const FText& Text){if(bUpdatingDraft)return;DraftInputError=FText::GetEmpty();if(auto O=TradeOwner.Pin())O->SetDraftAmount(DisplayedDraftToken,Text.ToString());RefreshDraft();}
void URPGTradeWidget::QuantityEdited(const FText&){if(!bUpdatingDraft)RefreshDraft();}
void URPGTradeWidget::SelectBagItem(FString Option,ESelectInfo::Type){if(bUpdatingDraft)return;const int32 I=BagLabels.IndexOfByKey(Option);SelectedBagItem=BagIds.IsValidIndex(I)?BagIds[I]:FGuid();RefreshDraft();}
void URPGTradeWidget::SelectOfferItem(FString Option,ESelectInfo::Type){if(bUpdatingDraft)return;const int32 I=OfferLabels.IndexOfByKey(Option);SelectedOfferItem=OfferIds.IsValidIndex(I)?OfferIds[I]:FGuid();RefreshDraft();}
void URPGTradeWidget::AddOfferItem()
{
    if(auto O=TradeOwner.Pin())if(!O->SetDraftItem(DisplayedDraftToken,SelectedBagItem,QuantityBox->GetText().ToString()))DraftInputError=NSLOCTEXT("RPGTrade","AddOfferFailed","수량 또는 물품 정보를 다시 확인하세요. 제안에는 최대 8종을 담을 수 있습니다.");
    RefreshDraft();
}
void URPGTradeWidget::RemoveOfferItem(){if(auto O=TradeOwner.Pin())O->RemoveDraftItem(DisplayedDraftToken,SelectedOfferItem);RefreshDraft();}
void URPGTradeWidget::RefreshTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Refresh(); }
void URPGTradeWidget::RetryTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Retry(); }
void URPGTradeWidget::AcceptTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Submit(ERPGTradeAction::Accept); }
void URPGTradeWidget::CancelTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Submit(ERPGTradeAction::Cancel); }
void URPGTradeWidget::WithdrawTrade() { if (auto Owner = TradeOwner.Pin()) Owner->Submit(ERPGTradeAction::Withdraw); }
void URPGTradeWidget::ReviewChanged(bool bChecked) { ReviewedToken = bChecked ? DisplayedReviewToken : FGuid(); RefreshView(); }
void URPGTradeWidget::ReadyTrade() { if (auto Owner = TradeOwner.Pin()) Owner->SubmitConsent(ERPGTradeAction::Ready,ReviewedToken); }
void URPGTradeWidget::ConfirmTrade() { if (auto Owner = TradeOwner.Pin()) Owner->SubmitConsent(ERPGTradeAction::Confirm,ReviewedToken); }
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
