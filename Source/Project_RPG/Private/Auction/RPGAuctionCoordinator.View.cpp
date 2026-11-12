#include "Auction/RPGAuctionCoordinator.h"

void FRPGAuctionCoordinator::ClearBrowse()
{
    Page.Reset();Detail.Reset();Review.Reset();PageQuery={};PageToken.Invalidate();DetailSelection.Invalidate();bPageMine=false;
    PageState=DetailState=ERPGAuctionReadState::Idle;
}
bool FRPGAuctionCoordinator::RecheckStorage()
{
    const auto KeepAlive=AsShared();
    if(!Sync() || bBusy)return false;
    const bool Ready=Reconcile();Notify();return Ready; // No HTTP, even if a repaired file restores an intent.
}
void FRPGAuctionCoordinator::ReadDelay(const FRPGAuctionHttpResult& R)
{
    UpdateTime();
    if(R.bCurrentPlayerContext && R.bTransportSuccessful && (R.StatusCode==429 || R.StatusCode==503))
        NextAttempt=Now+(FMath::IsFinite(R.RetryAfterSeconds)&&R.RetryAfterSeconds>0?FMath::Clamp(R.RetryAfterSeconds,1.0,3600.0):60.0);
}
bool FRPGAuctionCoordinator::ReadAllowed(bool bMine)
{
    return Sync() && Current.bAuthenticated && Current.bEligible && (bMine || Current.bEnabled)
        && !bBusy && !bStorageBlocked && !Client.GetPending() && Now>=NextAttempt && Transport->CanSend();
}
bool FRPGAuctionCoordinator::ReadPage(bool bMine)
{const auto KeepAlive=AsShared();return SearchPage(FRPGAuctionSearch{},bMine);}
bool FRPGAuctionCoordinator::NextPage(FGuid DisplayedPage)
{
    const auto KeepAlive=AsShared();
    if(!Sync() || !Page.IsSet() || PageState!=ERPGAuctionReadState::Ready || !DisplayedPage.IsValid()
        || DisplayedPage!=PageToken || Page->NextCursor.IsEmpty())return false;
    auto Query=PageQuery;Query.Cursor=Page->NextCursor;return SearchPage(Query,bPageMine);
}
bool FRPGAuctionCoordinator::SearchPage(const FRPGAuctionSearch& Query,bool bMine)
{
    if(!ReadAllowed(bMine))return false;
    ClearBrowse();PageQuery=Query;bPageMine=bMine;PageState=ERPGAuctionReadState::Loading;bBusy=true;
    const uint64 Expected=++Generation;TWeakPtr<FRPGAuctionCoordinator> Weak=AsShared();
    auto Done=[Weak,Expected,bMine](FRPGAuctionHttpResult R)
    {
        const auto S=Weak.Pin();if(!S || S->Generation!=Expected || !S->Sync() || S->Generation!=Expected)return;
        S->bBusy=false;S->PageState=ERPGAuctionReadState::Failed;FRPGAuctionPage Incoming;
        if(R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode==200 && RPGAuction::DecodePage(R.Body,bMine,Incoming))
        {S->Page=MoveTemp(Incoming);S->PageToken=FGuid::NewGuid();S->PageState=ERPGAuctionReadState::Ready;}
        S->ReadDelay(R);S->Notify();
    };
    const bool Sent=Transport->SendPage(Scope.Character,Query,bMine,Done);
    if(!Sent && Generation==Expected && bBusy)Done(FRPGAuctionHttpResult{});
    Notify();return Sent;
}
bool FRPGAuctionCoordinator::ReadDetail(FGuid DisplayedPage,FGuid Listing)
{
    const auto KeepAlive=AsShared();
    if(!ReadAllowed(bPageMine) || !Page.IsSet() || !DisplayedPage.IsValid() || DisplayedPage!=PageToken
        || !Page->Listings.ContainsByPredicate([&](const auto& L){return L.ListingId==Listing;}))return false;
    Detail.Reset();Review.Reset();DetailSelection=Listing;DetailState=ERPGAuctionReadState::Loading;bBusy=true;
    const bool Mine=bPageMine;const uint64 Expected=++Generation;TWeakPtr<FRPGAuctionCoordinator> Weak=AsShared();
    auto Done=[Weak,Expected,Listing,Mine,DisplayedPage](FRPGAuctionHttpResult R)
    {
        const auto S=Weak.Pin();if(!S || S->Generation!=Expected || !S->Sync() || S->Generation!=Expected)return;
        S->bBusy=false;S->DetailState=ERPGAuctionReadState::Failed;FRPGAuctionListing Incoming;
        // Public search hides seller identity, while own detail legitimately includes it.
        const bool Decoded=R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode==200
            && (RPGAuction::DecodeListing(R.Body,Mine,Incoming) || (!Mine && RPGAuction::DecodeListing(R.Body,true,Incoming)));
        if(Decoded && Incoming.ListingId==Listing && S->PageToken==DisplayedPage)
        {S->Detail=MoveTemp(Incoming);S->DetailState=ERPGAuctionReadState::Ready;}
        S->ReadDelay(R);S->Notify();
    };
    const bool Sent=Transport->SendListing(Scope.Character,Listing,Done);
    if(!Sent && Generation==Expected && bBusy)Done(FRPGAuctionHttpResult{});
    Notify();return Sent;
}
bool FRPGAuctionCoordinator::CanReviewAction(ERPGAuctionAction Action) const
{
    if(!Current.bAuthenticated || !Current.bEligible || bBusy || bStorageBlocked || ClearIntent.IsSet()
        || Client.GetPending() || Client.NeedsAssetRefresh() || Now<NextAttempt || !Transport->CanSend()
        || !Page.IsSet() || !PageToken.IsValid() || !Detail.IsSet() || DetailState!=ERPGAuctionReadState::Ready
        || Detail->Status!=ERPGAuctionStatus::Active
        || (Client.GetReceipt() && AssetCurrent && (AssetsState!=ERPGAuctionAssetsState::Ready || !Assets.IsSet() || !AssetCurrent())))return false;
    if(Action==ERPGAuctionAction::Cancel)return Detail->bOwn && Detail->SellerCharacter==Scope.Character;
    FText Name,Description;
    return Action==ERPGAuctionAction::Purchase && Current.bEnabled && !Detail->bOwn && Detail->CurrencyCode==TEXT("Gold")
        && ItemPresenter && ItemPresenter(Detail->Item,Name,Description) && !Name.IsEmpty();
}
bool FRPGAuctionCoordinator::ReviewListing(FGuid DisplayedPage,FGuid Listing,ERPGAuctionAction Action)
{
    const auto KeepAlive=AsShared();
    if(!Sync() || Review.IsSet() || DisplayedPage!=PageToken || !CanReviewAction(Action) || Detail->ListingId!=Listing)return false;
    const auto Text=Action==ERPGAuctionAction::Purchase
        ?FText::Format(NSLOCTEXT("RPGAuction","PurchaseReview","구매 확인\n{0}\n표시된 수량 전체를 표시된 총액으로 구매합니다. 가방이 부족하면 물품은 우편으로 전달됩니다."),ListingText(*Detail,true))
        :FText::Format(NSLOCTEXT("RPGAuction","CancelReview","판매 취소 확인\n{0}\n이 판매를 취소하고 물품을 돌려받습니다. 가방이 부족하면 우편으로 전달됩니다. 이미 판매되거나 종료된 경우 서버가 취소를 거절할 수 있습니다."),ListingText(*Detail,true));
    Review=FListingReview{*Detail,Action,FGuid::NewGuid(),Text};Notify();return true; // No intent or HTTP before final consent.
}
void FRPGAuctionCoordinator::DismissReview(FGuid DisplayedReview)
{
    const auto KeepAlive=AsShared();
    if(Review.IsSet() && Review->Token==DisplayedReview){Review.Reset();Notify();}
}
bool FRPGAuctionCoordinator::ConfirmListing(FGuid DisplayedReview)
{
    const auto KeepAlive=AsShared();
    if(!Sync() || !Review.IsSet() || !DisplayedReview.IsValid() || Review->Token!=DisplayedReview)return false;
    const auto ExpectedScope=Scope;const auto Accepted=*Review;
    // CanPrepare may reconcile disk or discover a new identity. Recheck the exact consent afterwards.
    if(!CanPrepare(Accepted.Action==ERPGAuctionAction::Purchase) || !(Scope==ExpectedScope)
        || !Review.IsSet() || Review->Token!=DisplayedReview || !CanReviewAction(Accepted.Action))
    {DismissReview(DisplayedReview);return false;}
    Review.Reset();
    const bool Prepared=Accepted.Action==ERPGAuctionAction::Purchase?Client.PreparePurchase(Accepted.Listing):Client.PrepareCancel(Accepted.Listing);
    if(Prepared)return SaveAndSend();
    Notify();return false;
}
FText FRPGAuctionCoordinator::ListingText(const FRPGAuctionListing& L,bool bDetail) const
{
    FText Name,Description;
    if(!ItemPresenter || !ItemPresenter(L.Item,Name,Description) || Name.IsEmpty())
    {Name=NSLOCTEXT("RPGAuction","UnknownItem","표시 정보를 확인할 수 없는 물품");Description=FText::GetEmpty();}
    const FText Status=L.Status==ERPGAuctionStatus::Active?NSLOCTEXT("RPGAuction","Active","판매 중")
        :L.Status==ERPGAuctionStatus::Sold?NSLOCTEXT("RPGAuction","Sold","판매 완료")
        :L.Status==ERPGAuctionStatus::Cancelled?NSLOCTEXT("RPGAuction","Cancelled","취소됨"):NSLOCTEXT("RPGAuction","Expired","판매 종료");
    const FText Price=L.CurrencyCode==TEXT("Gold")?FText::Format(NSLOCTEXT("RPGAuction","Gold","{0} 골드"),FText::AsNumber(L.TotalPrice))
        :NSLOCTEXT("RPGAuction","UnknownCurrency","가격 표시 정보 없음");
    const auto Row=FText::Format(NSLOCTEXT("RPGAuction","Row","{0} × {1} · {2} · {3}"),Name,FText::AsNumber(L.Quantity),Price,Status);
    if(!bDetail)return Row;
    return FText::Format(NSLOCTEXT("RPGAuction","Detail","{0}\n판매 종료 시각: {1}\n{2}"),Row,FText::AsDateTime(L.ExpiresAt),Description);
}
FRPGAuctionScreenView FRPGAuctionCoordinator::GetScreenView()
{
    const auto KeepAlive=AsShared();FRPGAuctionScreenView V;
    V.Status=NSLOCTEXT("RPGAuction","Unavailable","로비에서 캐릭터를 선택하고 로그인해 주세요.");
    if(!Sync())return V;
    V.Context=Scope;V.bAvailable=true;
    V.bPublicVisible=Current.bEnabled;V.bHasPending=Client.GetPending()!=nullptr;
    V.bStorageBlocked=bStorageBlocked;V.bHasReceipt=Client.GetReceipt()!=nullptr;
    const bool Ready=Current.bAuthenticated && !bBusy && !bStorageBlocked && Now>=NextAttempt && Transport->CanSend();
    V.bCanStorage=bStorageBlocked && !bBusy;
    V.bCanRecover=V.bCanRetry=Ready && Client.GetPending();
    V.bCanMine=Ready && Current.bEligible && !Client.GetPending();V.bCanPublic=V.bCanMine && Current.bEnabled;
    V.bCanNext=(bPageMine?V.bCanMine:V.bCanPublic) && Page.IsSet() && !Page->NextCursor.IsEmpty();
    V.bCanDetail=(bPageMine?V.bCanMine:V.bCanPublic) && Page.IsSet() && PageState==ERPGAuctionReadState::Ready;
    V.bCanAssets=Ready && Current.bEligible && !Client.GetPending() && Client.GetReceipt() && AssetBegin && AssetApply && AssetCurrent;
    V.bHasReview=Review.IsSet();V.bCanPurchase=!V.bHasReview && CanReviewAction(ERPGAuctionAction::Purchase);
    V.bCanCancel=!V.bHasReview && CanReviewAction(ERPGAuctionAction::Cancel);
    if(Review.IsSet()){V.ReviewToken=Review->Token;V.ReviewText=Review->Text;V.bCanConfirm=CanReviewAction(Review->Action);}
    if(bStorageBlocked)V.Status=NSLOCTEXT("RPGAuction","Storage","저장된 요청을 읽거나 정리할 수 없습니다. 원본을 유지하며 저장 상태를 다시 확인해 주세요.");
    else if(!Current.bAuthenticated)V.Status=NSLOCTEXT("RPGAuction","Auth","로그인이 만료되었습니다. 다시 로그인한 뒤 처리 결과를 확인해 주세요.");
    else if(bBusy)V.Status=NSLOCTEXT("RPGAuction","Busy","처리 결과를 확인하고 있습니다.");
    else if(Client.GetPending())V.Status=NSLOCTEXT("RPGAuction","Pending","이전 요청의 결과가 아직 확정되지 않았습니다. 먼저 처리 결과를 확인해 주세요.");
    else if(Now<NextAttempt)V.Status=NSLOCTEXT("RPGAuction","Wait","잠시 후 다시 확인해 주세요.");
    else if(PageState==ERPGAuctionReadState::Failed)V.Status=NSLOCTEXT("RPGAuction","PageFailed","내역을 불러오지 못했습니다. 다시 불러와 주세요.");
    else if(!Page.IsSet() && !Client.GetLastError().IsEmpty())V.Status=NSLOCTEXT("RPGAuction","CommandRejected","요청이 처리되지 않았습니다. 내역을 다시 불러와 현재 상태를 확인해 주세요.");
    else if(Page.IsSet())V.Status=Page->Listings.IsEmpty()?NSLOCTEXT("RPGAuction","Empty","표시할 내역이 없습니다.")
        :bPageMine?NSLOCTEXT("RPGAuction","Mine","내 판매 내역입니다. 상세 보기를 눌러 현재 상태를 확인하세요.")
        :NSLOCTEXT("RPGAuction","Public","판매 중인 물품입니다. 상세 보기를 눌러 현재 상태를 확인하세요.");
    else V.Status=Current.bEnabled?NSLOCTEXT("RPGAuction","Ready","판매 중인 물품 또는 내 판매 내역을 불러오세요.")
        :NSLOCTEXT("RPGAuction","RecoveryOnly","내 판매 내역 또는 이전 요청의 처리 결과를 확인할 수 있습니다.");
    if(Page.IsSet())
    {V.PageToken=PageToken;for(const auto& L:Page->Listings)V.Rows.Add({L.ListingId,ListingText(L,false)});}
    V.DetailListing=DetailSelection;
    if(Detail.IsSet())V.Detail=ListingText(*Detail,true);
    else if(DetailState==ERPGAuctionReadState::Failed)V.Detail=NSLOCTEXT("RPGAuction","DetailFailed","현재 상세 정보를 확인할 수 없습니다. 내역을 다시 불러와 주세요.");
    if(Client.GetReceipt())
    {
        V.Assets=Client.NeedsAssetRefresh()?NSLOCTEXT("RPGAuction","RefreshNeeded","요청 처리는 완료되었습니다. 최신 보유 물품과 재화를 불러와 주세요.")
            :NSLOCTEXT("RPGAuction","RefreshAgain","필요하면 최신 보유 물품과 재화를 다시 불러올 수 있습니다.");
        if(AssetsState==ERPGAuctionAssetsState::Failed)V.Assets=NSLOCTEXT("RPGAuction","AssetsFailed","최신 자산을 불러오지 못했습니다. 다시 불러와 주세요.");
        if(const auto* A=GetAssets())
        {
            V.Assets=FText::Format(NSLOCTEXT("RPGAuction","AssetsReady","최신 보유 물품 {0}칸 · 우편 물품 {1}칸을 확인했습니다."),FText::AsNumber(A->Items.Num()),FText::AsNumber(A->Mail.Num()));
            for(const auto& B:A->Wallet.Balances)if(B.CurrencyCode==TEXT("Gold"))
                V.Assets=FText::Format(NSLOCTEXT("RPGAuction","AssetsGold","{0}\n보유 골드: {1}"),V.Assets,FText::AsNumber(B.Balance));
        }
    }
    return V;
}
