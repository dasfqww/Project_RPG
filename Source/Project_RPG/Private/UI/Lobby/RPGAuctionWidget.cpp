#include "UI/Lobby/RPGAuctionWidget.h"
#include "UI/Lobby/RPGTradingWindowLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/EditableTextBox.h"
#include "InputCoreTypes.h"

TSharedRef<SWidget> URPGAuctionWidget::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("AuctionTree"));
    if(!WidgetTree->RootWidget)
    {
        const auto Frame=FRPGTradingWindowLayout::Build(*WidgetTree,TEXT("Auction"),FVector2D(680,720));
        WindowBounds=Frame.Bounds;WindowBorder=Frame.Border;
        auto* List=Frame.Header;
        auto Text=[&](const FText& Label){auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(Label);FRPGTradingWindowLayout::FormatText(*T);List->AddChild(T);return T;};
        auto Button=[&](const TCHAR* Name,const FText& Label){auto* B=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(Name));auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(Label);FRPGTradingWindowLayout::FormatText(*T);B->SetContent(T);List->AddChild(B);return B;};
        FRPGTradingWindowLayout::FormatText(*Text(NSLOCTEXT("RPGAuction","Title","경매 내역 · 처리 결과 확인")),true);
        List=Frame.Content;
        StatusText=Text(FText::GetEmpty());
        PublicButton=Button(TEXT("AuctionPublic"),NSLOCTEXT("RPGAuction","ReadPublic","판매 중인 물품 불러오기"));
        MineButton=Button(TEXT("AuctionMine"),NSLOCTEXT("RPGAuction","ReadMine","내 판매 내역 불러오기"));
        Text(NSLOCTEXT("RPGAuction","ChooseEntry","조회한 내역에서 항목을 선택하세요."));
        Rows=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("AuctionRows"));List->AddChild(Rows);
        DetailButton=Button(TEXT("AuctionDetail"),NSLOCTEXT("RPGAuction","ReadDetail","선택한 내역 상세 보기"));
        DetailText=Text(FText::GetEmpty());
        PurchaseButton=Button(TEXT("AuctionPurchaseReview"),NSLOCTEXT("RPGAuction","ReviewPurchase","구매 내용 확인"));
        CancelSaleButton=Button(TEXT("AuctionCancelReview"),NSLOCTEXT("RPGAuction","ReviewCancel","판매 취소 내용 확인"));
        PayableButton=Button(TEXT("AuctionReadPayable"),NSLOCTEXT("RPGAuction","ReadPayable","현재 수령 대기금 불러오기"));
        PayableText=Text(FText::GetEmpty());
        ClaimButton=Button(TEXT("AuctionClaimReview"),NSLOCTEXT("RPGAuction","ReviewClaim","대금 수령 내용 확인"));
        ClaimResultText=Text(FText::GetEmpty());
        auto* MainList=List;RegistrationPanel=WidgetTree->ConstructWidget<UVerticalBox>();List->AddChild(RegistrationPanel);List=RegistrationPanel;
        Text(NSLOCTEXT("RPGAuction","RegisterTitle","가방 물품 경매 등록"));
        PolicyButton=Button(TEXT("AuctionPolicy"),NSLOCTEXT("RPGAuction","ReadPolicy","서버 등록 조건 불러오기"));PolicyText=Text(FText::GetEmpty());
        BagButton=Button(TEXT("AuctionBag"),NSLOCTEXT("RPGAuction","ReadBag","등록할 최신 가방 불러오기"));RegistrationStatus=Text(FText::GetEmpty());
        BagRows=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("AuctionBagRows"));List->AddChild(BagRows);
        auto Input=[&](const FText& Label,const TCHAR* Value){Text(Label);auto* E=WidgetTree->ConstructWidget<UEditableTextBox>();E->SetText(FText::FromString(Value));List->AddChild(E);return E;};
        QuantityInput=Input(NSLOCTEXT("RPGAuction","Quantity","등록 수량 (정수)"),TEXT("1"));
        PriceInput=Input(NSLOCTEXT("RPGAuction","TotalPrice","전체 수량의 총 판매 가격 (골드, 정수)"),TEXT(""));
        DurationInput=Input(NSLOCTEXT("RPGAuction","DurationSeconds","판매 기간 (초, 정수 · 1시간=3600초, 하루=86400초)"),TEXT("86400"));
        RegisterButton=Button(TEXT("AuctionRegisterReview"),NSLOCTEXT("RPGAuction","ReviewRegistration","등록 내용 확인"));List=MainList;
        ReviewText=Text(FText::GetEmpty());
        ConfirmButton=Button(TEXT("AuctionConfirm"),NSLOCTEXT("RPGAuction","Confirm","위 내용으로 실행"));
        DismissButton=Button(TEXT("AuctionDismiss"),NSLOCTEXT("RPGAuction","Dismiss","실행하지 않고 돌아가기"));
        NextButton=Button(TEXT("AuctionNext"),NSLOCTEXT("RPGAuction","Next","다음 내역 보기"));
        RecoverButton=Button(TEXT("AuctionRecover"),NSLOCTEXT("RPGAuction","Recover","이전 요청의 처리 결과 확인"));
        RetryButton=Button(TEXT("AuctionRetry"),NSLOCTEXT("RPGAuction","Retry","이전 요청 그대로 재시도"));
        RetryHelpText=Text(NSLOCTEXT("RPGAuction","RetryHelp","결과가 미확정이면 먼저 처리 결과를 확인하세요. 재시도하면 이전 요청 내용으로 처리될 수 있습니다."));
        StorageButton=Button(TEXT("AuctionStorage"),NSLOCTEXT("RPGAuction","CheckStorage","저장 상태 다시 확인"));
        AssetText=Text(FText::GetEmpty());AssetsButton=Button(TEXT("AuctionAssets"),NSLOCTEXT("RPGAuction","ReloadAssets","최신 물품 · 우편 · 재화 불러오기"));
        List=Frame.Footer;
        CloseButton=Button(TEXT("AuctionClose"),NSLOCTEXT("RPGAuction","Close","닫기"));
        PublicButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadPublic);MineButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadMine);
        NextButton->OnClicked.AddUniqueDynamic(this,&ThisClass::NextPage);DetailButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadDetail);
        PurchaseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReviewPurchase);CancelSaleButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReviewCancel);
        PayableButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadPayable);ClaimButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReviewClaim);
        PolicyButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadPolicy);BagButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReloadBag);RegisterButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReviewRegistration);
        BagRows->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SelectBag);
        QuantityInput->OnTextChanged.AddUniqueDynamic(this,&ThisClass::RegistrationInputChanged);PriceInput->OnTextChanged.AddUniqueDynamic(this,&ThisClass::RegistrationInputChanged);DurationInput->OnTextChanged.AddUniqueDynamic(this,&ThisClass::RegistrationInputChanged);
        ConfirmButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Confirm);DismissButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Dismiss);
        RecoverButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Recover);RetryButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Retry);
        StorageButton->OnClicked.AddUniqueDynamic(this,&ThisClass::CheckStorage);AssetsButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReloadAssets);
        CloseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Close);Rows->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SelectRow);
    }
    DisplayedPage.Invalidate();DisplayedBag.Invalidate();SetIsFocusable(true);RefreshView();return Super::RebuildWidget();
}
void URPGAuctionWidget::Subscribe()
{if(const auto C=Owner.Pin()){C->OnViewChanged.Remove(ChangeHandle);ChangeHandle=C->OnViewChanged.AddUObject(this,&ThisClass::RefreshView);}}
void URPGAuctionWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{Super::NativeTick(Geometry,DeltaSeconds);FRPGTradingWindowLayout::Apply(WindowBounds,WindowBorder,Geometry.GetLocalSize(),FVector2D(680,720));}
void URPGAuctionWidget::SetOwner(TSharedRef<FRPGAuctionCoordinator> InOwner)
{
    if(const auto Old=Owner.Pin()){Old->OnViewChanged.Remove(ChangeHandle);Old->DismissReview(DisplayedReview);}
    DisplayedReview.Invalidate();
    DisplayedBag.Invalidate();SelectedBag.Invalidate();BagIds.Reset();
    if(BagRows){bUpdating=true;BagRows->ClearSelection();BagRows->ClearOptions();bUpdating=false;}
    Owner=InOwner;BoundContext={};DisplayedPage.Invalidate();Selected.Invalidate();RowIds.Reset();bClosing=false;bUpdating=true;
    if(Rows){Rows->ClearSelection();Rows->ClearOptions();}bUpdating=false;
    Subscribe();RefreshView();
}
void URPGAuctionWidget::NativeConstruct()
{Super::NativeConstruct();bClosing=false;Subscribe();RefreshView();if(CloseButton && GetOwningPlayer())CloseButton->SetKeyboardFocus();}
void URPGAuctionWidget::NativeDestruct()
{if(const auto C=Owner.Pin()){C->OnViewChanged.Remove(ChangeHandle);C->DismissReview(DisplayedReview);}DisplayedReview.Invalidate();ChangeHandle.Reset();Super::NativeDestruct();}
void URPGAuctionWidget::RefreshView()
{
    if(bClosing)return;
    const auto C=Owner.Pin();const auto V=C?C->GetScreenView():FRPGAuctionScreenView{};
    if(BoundContext.IsValid() && !(BoundContext==V.Context)){Close();return;}
    if(V.Context.IsValid())BoundContext=V.Context;
    if(StatusText)StatusText->SetText(C?V.Status:NSLOCTEXT("RPGAuction","NoOwner","경매 내역을 확인할 수 없습니다."));
    if(RegistrationPanel)RegistrationPanel->SetVisibility(V.bRegistrationVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(PolicyText)PolicyText->SetText(V.PolicyText);if(RegistrationStatus)RegistrationStatus->SetText(V.RegistrationStatus);
    if(PolicyButton)PolicyButton->SetIsEnabled(V.bCanReadPolicy);if(BagButton)BagButton->SetIsEnabled(V.bCanReloadBag);
    if(DisplayedBag!=V.BagToken)
    {
        DisplayedBag=V.BagToken;SelectedBag.Invalidate();BagIds.Reset();bUpdating=true;
        if(BagRows){BagRows->ClearSelection();BagRows->ClearOptions();for(int32 I=0;I<V.BagItems.Num();++I){const auto& Item=V.BagItems[I];BagIds.Add(Item.ItemId);BagRows->AddOption(FString::Printf(TEXT("%d. %s · %d개"),I+1,*Item.Label.ToString(),Item.Quantity));}}
        bUpdating=false;
    }
    if(RegisterButton)RegisterButton->SetIsEnabled(V.bCanReviewRegistration && SelectedBag.IsValid());
    if(DetailText)DetailText->SetText(Selected.IsValid() && Selected==V.DetailListing?V.Detail:FText::GetEmpty());if(AssetText)AssetText->SetText(V.Assets);
    DisplayedReview=V.ReviewToken;
    if(ReviewText){ReviewText->SetText(V.ReviewText);ReviewText->SetVisibility(V.bHasReview?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
    if(ConfirmButton){ConfirmButton->SetVisibility(V.bHasReview?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ConfirmButton->SetIsEnabled(V.bCanConfirm);}
    if(DismissButton)DismissButton->SetVisibility(V.bHasReview?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(PurchaseButton){PurchaseButton->SetVisibility(V.bPublicVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);PurchaseButton->SetIsEnabled(V.bCanPurchase && Selected==V.DetailListing);}
    if(CancelSaleButton)CancelSaleButton->SetIsEnabled(V.bCanCancel && Selected==V.DetailListing);
    const bool ShowPayable=V.bPayableVisible && Selected.IsValid() && Selected==V.DetailListing;
    if(PayableButton){PayableButton->SetVisibility(ShowPayable?ESlateVisibility::Visible:ESlateVisibility::Collapsed);PayableButton->SetIsEnabled(V.bCanReadPayable);}
    if(ClaimButton){ClaimButton->SetVisibility(ShowPayable?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ClaimButton->SetIsEnabled(V.bCanClaim);}
    if(PayableText){PayableText->SetText(ShowPayable?V.PayableText:FText::GetEmpty());PayableText->SetVisibility(ShowPayable?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
    if(ClaimResultText){ClaimResultText->SetText(V.ClaimResult);ClaimResultText->SetVisibility(V.ClaimResult.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::Visible);}
    if(PublicButton)PublicButton->SetIsEnabled(V.bCanPublic);if(MineButton)MineButton->SetIsEnabled(V.bCanMine);
    if(NextButton)NextButton->SetIsEnabled(V.bCanNext);
    if(RecoverButton)RecoverButton->SetIsEnabled(V.bCanRecover);if(RetryButton)RetryButton->SetIsEnabled(V.bCanRetry);
    if(StorageButton)StorageButton->SetIsEnabled(V.bCanStorage);if(AssetsButton)AssetsButton->SetIsEnabled(V.bCanAssets);
    if(PublicButton)PublicButton->SetVisibility(V.bPublicVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(RecoverButton)RecoverButton->SetVisibility(V.bHasPending?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(RetryButton)RetryButton->SetVisibility(V.bHasPending?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(RetryHelpText)RetryHelpText->SetVisibility(V.bHasPending?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(StorageButton)StorageButton->SetVisibility(V.bStorageBlocked?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(AssetText)AssetText->SetVisibility(V.bHasReceipt?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(AssetsButton)AssetsButton->SetVisibility(V.bHasReceipt?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(DisplayedPage!=V.PageToken)
    {
        DisplayedPage=V.PageToken;Selected.Invalidate();RowIds.Reset();bUpdating=true;
        if(Rows){Rows->ClearSelection();Rows->ClearOptions();for(int32 I=0;I<V.Rows.Num();++I)
        {RowIds.Add(V.Rows[I].ListingId);Rows->AddOption(FString::Printf(TEXT("%d. %s"),I+1,*V.Rows[I].Label.ToString()));}}
        bUpdating=false;
    }
    if(Rows)Rows->SetIsEnabled(V.bCanDetail);
    if(DetailButton)DetailButton->SetIsEnabled(V.bCanDetail && Selected.IsValid());
}
TSharedPtr<FRPGAuctionCoordinator> URPGAuctionWidget::ActiveOwner()
{
    if(bClosing)return nullptr;
    const auto C=Owner.Pin();
    // Revalidate before dispatch, even when the next view tick has not run yet.
    if(!C || !BoundContext.IsValid() || !(BoundContext==C->GetScreenView().Context)){Close();return nullptr;}
    return C;
}
void URPGAuctionWidget::SelectRow(FString,ESelectInfo::Type)
{if(bUpdating || bClosing)return;if(const auto C=Owner.Pin())C->DismissReview(DisplayedReview);Selected.Invalidate();if(Rows && RowIds.IsValidIndex(Rows->GetSelectedIndex()))Selected=RowIds[Rows->GetSelectedIndex()];RefreshView();}
void URPGAuctionWidget::ReadPublic(){if(const auto C=ActiveOwner())C->ReadPage(false);RefreshView();}
void URPGAuctionWidget::ReadMine(){if(const auto C=ActiveOwner())C->ReadPage(true);RefreshView();}
void URPGAuctionWidget::NextPage(){if(const auto C=ActiveOwner())C->NextPage(DisplayedPage);RefreshView();}
void URPGAuctionWidget::ReadDetail(){if(const auto C=ActiveOwner())C->ReadDetail(DisplayedPage,Selected);RefreshView();}
void URPGAuctionWidget::ReviewPurchase(){if(const auto C=ActiveOwner())C->ReviewListing(DisplayedPage,Selected,ERPGAuctionAction::Purchase);RefreshView();}
void URPGAuctionWidget::ReviewCancel(){if(const auto C=ActiveOwner())C->ReviewListing(DisplayedPage,Selected,ERPGAuctionAction::Cancel);RefreshView();}
void URPGAuctionWidget::ReadPayable(){if(const auto C=ActiveOwner())C->ReadPayable(DisplayedPage,Selected);RefreshView();}
void URPGAuctionWidget::ReviewClaim(){if(const auto C=ActiveOwner())C->ReviewListing(DisplayedPage,Selected,ERPGAuctionAction::Claim);RefreshView();}
void URPGAuctionWidget::ReadPolicy(){if(const auto C=ActiveOwner())C->ReadRegistrationPolicy();RefreshView();}
void URPGAuctionWidget::ReloadBag(){if(const auto C=ActiveOwner())C->ReloadRegistrationBag();RefreshView();}
void URPGAuctionWidget::SelectBag(FString,ESelectInfo::Type)
{if(bUpdating || bClosing)return;if(const auto C=Owner.Pin())C->DismissReview(DisplayedReview);SelectedBag.Invalidate();if(BagRows && BagIds.IsValidIndex(BagRows->GetSelectedIndex()))SelectedBag=BagIds[BagRows->GetSelectedIndex()];RefreshView();}
void URPGAuctionWidget::RegistrationInputChanged(const FText&)
{if(bUpdating || bClosing)return;if(const auto C=Owner.Pin())C->DismissReview(DisplayedReview);RefreshView();}
void URPGAuctionWidget::ReviewRegistration()
{if(const auto C=ActiveOwner())C->ReviewRegistration(DisplayedBag,SelectedBag,QuantityInput->GetText().ToString(),PriceInput->GetText().ToString(),DurationInput->GetText().ToString());RefreshView();}
void URPGAuctionWidget::Confirm(){if(const auto C=ActiveOwner())C->ConfirmListing(DisplayedReview);RefreshView();}
void URPGAuctionWidget::Dismiss(){if(const auto C=ActiveOwner())C->DismissReview(DisplayedReview);RefreshView();}
void URPGAuctionWidget::Recover(){if(const auto C=ActiveOwner())C->Recover();RefreshView();}
void URPGAuctionWidget::Retry(){if(const auto C=ActiveOwner())C->Retry();RefreshView();}
void URPGAuctionWidget::CheckStorage(){if(const auto C=ActiveOwner())C->RecheckStorage();RefreshView();}
void URPGAuctionWidget::ReloadAssets(){if(const auto C=ActiveOwner())C->ReloadAssets();RefreshView();}
void URPGAuctionWidget::CloseScreen(){Close();}
void URPGAuctionWidget::Close()
{if(bClosing)return;bClosing=true;if(const auto C=Owner.Pin()){C->OnViewChanged.Remove(ChangeHandle);C->DismissReview(DisplayedReview);}DisplayedReview.Invalidate();ChangeHandle.Reset();if(IsInViewport() || GetParent())RemoveFromParent();OnClosed.Broadcast();}
FReply URPGAuctionWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{if(E.GetKey()==EKeys::Escape){if(!E.IsRepeat())Close();return FReply::Handled();}return Super::NativeOnKeyDown(G,E);}
