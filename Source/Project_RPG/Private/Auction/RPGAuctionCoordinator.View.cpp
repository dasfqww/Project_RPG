#include "Auction/RPGAuctionCoordinator.h"

void FRPGAuctionCoordinator::ClearBrowse()
{
    Page.Reset();Detail.Reset();Payable.Reset();Review.Reset();RegistrationReview.Reset();PageQuery={};PageToken.Invalidate();DetailSelection.Invalidate();bPageMine=false;
    PageState=DetailState=PayableState=ERPGAuctionReadState::Idle;
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
    Detail.Reset();Payable.Reset();Review.Reset();RegistrationReview.Reset();PayableState=ERPGAuctionReadState::Idle;DetailSelection=Listing;DetailState=ERPGAuctionReadState::Loading;bBusy=true;
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
bool FRPGAuctionCoordinator::OwnSoldDetail() const
{
    return Page.IsSet() && PageToken.IsValid() && Detail.IsSet() && DetailState==ERPGAuctionReadState::Ready
        && Detail->ListingId==DetailSelection && Detail->bOwn && Detail->SellerCharacter==Scope.Character && Detail->Status==ERPGAuctionStatus::Sold;
}
bool FRPGAuctionCoordinator::ReadPayable(FGuid DisplayedPage,FGuid Listing)
{
    const auto KeepAlive=AsShared();
    if(!ReadAllowed(true) || !OwnSoldDetail() || DisplayedPage!=PageToken || Detail->ListingId!=Listing)return false;
    Payable.Reset();Review.Reset();RegistrationReview.Reset();PayableState=ERPGAuctionReadState::Loading;bBusy=true;
    const uint64 Expected=++Generation;TWeakPtr<FRPGAuctionCoordinator> Weak=AsShared();
    auto Done=[Weak,Expected,Listing](FRPGAuctionHttpResult R)
    {
        const auto S=Weak.Pin();if(!S || S->Generation!=Expected || !S->Sync() || S->Generation!=Expected)return;
        S->bBusy=false;S->PayableState=ERPGAuctionReadState::Failed;FRPGAuctionPayable Incoming;
        if(R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode==200 && S->OwnSoldDetail()
            && RPGAuction::DecodePayable(R.Body,Incoming) && Incoming.ListingId==Listing && Incoming.CurrencyCode==S->Detail->CurrencyCode
            && S->Detail->SellerProceeds.IsSet() && Incoming.Initial==S->Detail->SellerProceeds.GetValue())
        {S->Payable=MoveTemp(Incoming);S->PayableState=ERPGAuctionReadState::Ready;}
        S->ReadDelay(R);S->Notify();
    };
    const bool Sent=Transport->SendPayable(Scope.Character,Listing,Done);
    if(!Sent && Generation==Expected && bBusy)Done(FRPGAuctionHttpResult{});
    Notify();return Sent;
}
bool FRPGAuctionCoordinator::CanReviewAction(ERPGAuctionAction Action) const
{
    if(!NewCommandAllowed(Action==ERPGAuctionAction::Purchase) || !Page.IsSet() || !PageToken.IsValid() || !Detail.IsSet() || DetailState!=ERPGAuctionReadState::Ready)return false;
    if(Action==ERPGAuctionAction::Claim)return OwnSoldDetail() && Payable.IsSet() && PayableState==ERPGAuctionReadState::Ready
        && Payable->ListingId==Detail->ListingId && Payable->CurrencyCode==TEXT("Gold") && Payable->Remaining>0;
    if(Detail->Status!=ERPGAuctionStatus::Active)return false;
    if(Action==ERPGAuctionAction::Cancel)return Detail->bOwn && Detail->SellerCharacter==Scope.Character;
    FText Name,Description;
    return Action==ERPGAuctionAction::Purchase && Current.bEnabled && !Detail->bOwn && Detail->CurrencyCode==TEXT("Gold")
        && ItemPresenter && ItemPresenter(Detail->Item,Name,Description) && !Name.IsEmpty();
}
bool FRPGAuctionCoordinator::ReviewListing(FGuid DisplayedPage,FGuid Listing,ERPGAuctionAction Action)
{
    const auto KeepAlive=AsShared();
    if(!Sync() || Review.IsSet() || RegistrationReview.IsSet() || DisplayedPage!=PageToken || !CanReviewAction(Action) || Detail->ListingId!=Listing)return false;
    const auto Text=Action==ERPGAuctionAction::Claim
        ?FText::Format(NSLOCTEXT("RPGAuction","ClaimReview","판매 대금 수령 확인\n{0}\n현재 수령 대기금 {1} 골드 전액을 요청합니다. 보유 한도에 따라 일부만 수령할 수 있으며, 남은 금액은 대기금으로 유지됩니다."),ListingText(*Detail,false),FText::AsNumber(Payable->Remaining))
        :Action==ERPGAuctionAction::Purchase
        ?FText::Format(NSLOCTEXT("RPGAuction","PurchaseReview","구매 확인\n{0}\n표시된 수량 전체를 표시된 총액으로 구매합니다. 가방이 부족하면 물품은 우편으로 전달됩니다."),ListingText(*Detail,true))
        :FText::Format(NSLOCTEXT("RPGAuction","CancelReview","판매 취소 확인\n{0}\n이 판매를 취소하고 물품을 돌려받습니다. 가방이 부족하면 우편으로 전달됩니다. 이미 판매되거나 종료된 경우 서버가 취소를 거절할 수 있습니다."),ListingText(*Detail,true));
    Review=FListingReview{*Detail,Action,FGuid::NewGuid(),Text,Action==ERPGAuctionAction::Claim?Payable:TOptional<FRPGAuctionPayable>{}};Notify();return true; // No intent or HTTP before final consent.
}
void FRPGAuctionCoordinator::DismissReview(FGuid DisplayedReview)
{
    const auto KeepAlive=AsShared();
    if(RegistrationReview.IsSet() && RegistrationReview->Token==DisplayedReview){RegistrationReview.Reset();Notify();}
    if(Review.IsSet() && Review->Token==DisplayedReview){Review.Reset();Notify();}
}
bool FRPGAuctionCoordinator::ConfirmListing(FGuid DisplayedReview)
{
    const auto KeepAlive=AsShared();
    if(RegistrationReview.IsSet() && RegistrationReview->Token==DisplayedReview)return ConfirmRegistration(DisplayedReview);
    if(!Sync() || !Review.IsSet() || !DisplayedReview.IsValid() || Review->Token!=DisplayedReview)return false;
    const auto ExpectedScope=Scope;const auto Accepted=*Review;
    // CanPrepare may reconcile disk or discover a new identity. Recheck the exact consent afterwards.
    if(!CanPrepare(Accepted.Action==ERPGAuctionAction::Purchase) || !(Scope==ExpectedScope)
        || !Review.IsSet() || Review->Token!=DisplayedReview || !CanReviewAction(Accepted.Action))
    {DismissReview(DisplayedReview);return false;}
    Review.Reset();
    const bool Prepared=Accepted.Action==ERPGAuctionAction::Claim
        ?Accepted.Payable.IsSet() && Client.PrepareClaim(*Accepted.Payable,Accepted.Payable->Remaining)
        :Accepted.Action==ERPGAuctionAction::Purchase?Client.PreparePurchase(Accepted.Listing):Client.PrepareCancel(Accepted.Listing);
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
    V.bHasReview=Review.IsSet() || RegistrationReview.IsSet();V.bCanPurchase=!V.bHasReview && CanReviewAction(ERPGAuctionAction::Purchase);
    V.bCanCancel=!V.bHasReview && CanReviewAction(ERPGAuctionAction::Cancel);
    V.bPayableVisible=OwnSoldDetail();V.bCanReadPayable=V.bCanMine && V.bPayableVisible;
    V.bCanClaim=!V.bHasReview && CanReviewAction(ERPGAuctionAction::Claim);
    if(V.bPayableVisible)
    {
        V.PayableText=NSLOCTEXT("RPGAuction","PayablePrompt","판매 대금을 수령하려면 현재 수령 대기금을 불러오세요.");
        if(PayableState==ERPGAuctionReadState::Loading)V.PayableText=NSLOCTEXT("RPGAuction","PayableLoading","현재 수령 대기금을 확인하고 있습니다.");
        else if(PayableState==ERPGAuctionReadState::Failed)V.PayableText=NSLOCTEXT("RPGAuction","PayableFailed","수령 대기금을 불러오지 못했습니다. 다시 확인해 주세요.");
        else if(Payable.IsSet())V.PayableText=Payable->CurrencyCode!=TEXT("Gold")?NSLOCTEXT("RPGAuction","PayableCurrency","수령 재화의 표시 정보를 확인할 수 없습니다.")
            :Payable->Remaining==0?NSLOCTEXT("RPGAuction","PayableEmpty","수령할 대기금이 없습니다.")
            :FText::Format(NSLOCTEXT("RPGAuction","PayableReady","현재 수령 대기금: {0} 골드"),FText::AsNumber(Payable->Remaining));
    }
    if(Review.IsSet()){V.ReviewToken=Review->Token;V.ReviewText=Review->Text;V.bCanConfirm=CanReviewAction(Review->Action);}
    RegistrationView(V);
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
        const auto* Receipt=Client.GetReceipt();
        if(Receipt->Action==ERPGAuctionAction::Claim)V.ClaimResult=FText::Format(NSLOCTEXT("RPGAuction","ClaimResult","최근 수령 결과: 지급액 {0} · 처리 당시 남은 대기금 {1}\n현재 보유 재화는 최신 조회로 확인하세요."),FText::AsNumber(Receipt->Paid),FText::AsNumber(Receipt->Remaining));
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
