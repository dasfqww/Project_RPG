#include "UI/Lobby/RPGAuctionWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "InputCoreTypes.h"

TSharedRef<SWidget> URPGAuctionWidget::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("AuctionTree"));
    if(!WidgetTree->RootWidget)
    {
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
        auto* Shade=WidgetTree->ConstructWidget<UBorder>();Shade->SetBrushColor(FLinearColor(0,0,0,.75f));
        auto* ShadeSlot=Canvas->AddChildToCanvas(Shade);ShadeSlot->SetAnchors(FAnchors(0,0,1,1));ShadeSlot->SetOffsets(FMargin(0));
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(680);Size->SetMaxDesiredHeight(720);
        auto* DialogSlot=Canvas->AddChildToCanvas(Size);DialogSlot->SetAnchors(FAnchors(.5f));DialogSlot->SetAlignment(FVector2D(.5f));DialogSlot->SetAutoSize(true);
        auto* Panel=WidgetTree->ConstructWidget<UBorder>();Panel->SetPadding(FMargin(24));Panel->SetBrushColor(FLinearColor(.035f,.05f,.07f,1));Size->AddChild(Panel);
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Panel->SetContent(Scroll);
        auto* List=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(List);
        auto Text=[&](const FText& Label){auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(Label);T->SetAutoWrapText(true);List->AddChild(T);return T;};
        auto Button=[&](const TCHAR* Name,const FText& Label){auto* B=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(Name));auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(Label);B->SetContent(T);List->AddChild(B);return B;};
        Text(NSLOCTEXT("RPGAuction","Title","경매 내역 · 처리 결과 확인"));
        StatusText=Text(FText::GetEmpty());
        PublicButton=Button(TEXT("AuctionPublic"),NSLOCTEXT("RPGAuction","ReadPublic","판매 중인 물품 불러오기"));
        MineButton=Button(TEXT("AuctionMine"),NSLOCTEXT("RPGAuction","ReadMine","내 판매 내역 불러오기"));
        Text(NSLOCTEXT("RPGAuction","ChooseEntry","조회한 내역에서 항목을 선택하세요."));
        Rows=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("AuctionRows"));List->AddChild(Rows);
        DetailButton=Button(TEXT("AuctionDetail"),NSLOCTEXT("RPGAuction","ReadDetail","선택한 내역 상세 보기"));
        DetailText=Text(FText::GetEmpty());
        NextButton=Button(TEXT("AuctionNext"),NSLOCTEXT("RPGAuction","Next","다음 내역 보기"));
        RecoverButton=Button(TEXT("AuctionRecover"),NSLOCTEXT("RPGAuction","Recover","이전 요청의 처리 결과 확인"));
        RetryButton=Button(TEXT("AuctionRetry"),NSLOCTEXT("RPGAuction","Retry","이전 요청 그대로 재시도"));
        RetryHelpText=Text(NSLOCTEXT("RPGAuction","RetryHelp","결과가 미확정이면 먼저 처리 결과를 확인하세요. 재시도하면 이전 요청 내용으로 처리될 수 있습니다."));
        StorageButton=Button(TEXT("AuctionStorage"),NSLOCTEXT("RPGAuction","CheckStorage","저장 상태 다시 확인"));
        AssetText=Text(FText::GetEmpty());AssetsButton=Button(TEXT("AuctionAssets"),NSLOCTEXT("RPGAuction","ReloadAssets","최신 물품 · 우편 · 재화 불러오기"));
        CloseButton=Button(TEXT("AuctionClose"),NSLOCTEXT("RPGAuction","Close","닫기"));
        PublicButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadPublic);MineButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadMine);
        NextButton->OnClicked.AddUniqueDynamic(this,&ThisClass::NextPage);DetailButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadDetail);
        RecoverButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Recover);RetryButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Retry);
        StorageButton->OnClicked.AddUniqueDynamic(this,&ThisClass::CheckStorage);AssetsButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReloadAssets);
        CloseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Close);Rows->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SelectRow);
    }
    DisplayedPage.Invalidate();SetIsFocusable(true);RefreshView();return Super::RebuildWidget();
}
void URPGAuctionWidget::Subscribe()
{if(const auto C=Owner.Pin()){C->OnViewChanged.Remove(ChangeHandle);ChangeHandle=C->OnViewChanged.AddUObject(this,&ThisClass::RefreshView);}}
void URPGAuctionWidget::SetOwner(TSharedRef<FRPGAuctionCoordinator> InOwner)
{
    if(const auto Old=Owner.Pin())Old->OnViewChanged.Remove(ChangeHandle);
    Owner=InOwner;BoundContext={};DisplayedPage.Invalidate();Selected.Invalidate();RowIds.Reset();bClosing=false;bUpdating=true;
    if(Rows){Rows->ClearSelection();Rows->ClearOptions();}bUpdating=false;
    Subscribe();RefreshView();
}
void URPGAuctionWidget::NativeConstruct()
{Super::NativeConstruct();bClosing=false;Subscribe();RefreshView();if(CloseButton && GetOwningPlayer())CloseButton->SetKeyboardFocus();}
void URPGAuctionWidget::NativeDestruct()
{if(const auto C=Owner.Pin())C->OnViewChanged.Remove(ChangeHandle);ChangeHandle.Reset();Super::NativeDestruct();}
void URPGAuctionWidget::RefreshView()
{
    if(bClosing)return;
    const auto C=Owner.Pin();const auto V=C?C->GetScreenView():FRPGAuctionScreenView{};
    if(BoundContext.IsValid() && !(BoundContext==V.Context)){Close();return;}
    if(V.Context.IsValid())BoundContext=V.Context;
    if(StatusText)StatusText->SetText(C?V.Status:NSLOCTEXT("RPGAuction","NoOwner","경매 내역을 확인할 수 없습니다."));
    if(DetailText)DetailText->SetText(V.Detail);if(AssetText)AssetText->SetText(V.Assets);
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
{if(bUpdating || bClosing)return;Selected.Invalidate();if(Rows && RowIds.IsValidIndex(Rows->GetSelectedIndex()))Selected=RowIds[Rows->GetSelectedIndex()];RefreshView();}
void URPGAuctionWidget::ReadPublic(){if(const auto C=ActiveOwner())C->ReadPage(false);RefreshView();}
void URPGAuctionWidget::ReadMine(){if(const auto C=ActiveOwner())C->ReadPage(true);RefreshView();}
void URPGAuctionWidget::NextPage(){if(const auto C=ActiveOwner())C->NextPage(DisplayedPage);RefreshView();}
void URPGAuctionWidget::ReadDetail(){if(const auto C=ActiveOwner())C->ReadDetail(DisplayedPage,Selected);RefreshView();}
void URPGAuctionWidget::Recover(){if(const auto C=ActiveOwner())C->Recover();RefreshView();}
void URPGAuctionWidget::Retry(){if(const auto C=ActiveOwner())C->Retry();RefreshView();}
void URPGAuctionWidget::CheckStorage(){if(const auto C=ActiveOwner())C->RecheckStorage();RefreshView();}
void URPGAuctionWidget::ReloadAssets(){if(const auto C=ActiveOwner())C->ReloadAssets();RefreshView();}
void URPGAuctionWidget::CloseScreen(){Close();}
void URPGAuctionWidget::Close()
{if(bClosing)return;bClosing=true;if(const auto C=Owner.Pin())C->OnViewChanged.Remove(ChangeHandle);ChangeHandle.Reset();if(IsInViewport() || GetParent())RemoveFromParent();OnClosed.Broadcast();}
FReply URPGAuctionWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{if(E.GetKey()==EKeys::Escape){if(!E.IsRepeat())Close();return FReply::Handled();}return Super::NativeOnKeyDown(G,E);}
