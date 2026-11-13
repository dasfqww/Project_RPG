#include "Auction/RPGAuctionCoordinator.h"

bool FRPGAuctionCoordinator::NewCommandAllowed(bool bNewSale) const
{
    return Scope.IsValid() && Current.bAuthenticated && Current.bEligible && (!bNewSale || Current.bEnabled)
        && !bBusy && !bStorageBlocked && !ClearIntent.IsSet() && !Client.GetPending() && !Client.NeedsAssetRefresh()
        && Now>=NextAttempt && Transport->CanSend()
        && (!Client.GetReceipt() || !AssetCurrent || (AssetsState==ERPGAuctionAssetsState::Ready && Assets.IsSet() && AssetCurrent()));
}
void FRPGAuctionCoordinator::SetRegistrationBagSource(TFunction<FRPGTradeBagSnapshot()> Read,TFunction<bool()> Reload)
{RegistrationBagReader=MoveTemp(Read);RegistrationBagReload=MoveTemp(Reload);}
bool FRPGAuctionCoordinator::ReadRegistrationBag(FRPGTradeBagSnapshot& Out) const
{
    if(!RegistrationBagReader)return false;
    Out=RegistrationBagReader();FRPGTradeIntentScope BagScope;
    return Out.IsUsable(Now) && FRPGTradeIntentScope::Create(Out.Endpoint,Out.Account,Out.Character,BagScope) && BagScope==Scope;
}
bool FRPGAuctionCoordinator::RegistrationPolicyCurrent() const
{return RegistrationPolicy.IsSet() && RegistrationPolicyState==ERPGAuctionReadState::Ready && RegistrationPolicy->CurrencyCode==TEXT("Gold") && Now>=PolicyReceived && Now-PolicyReceived<60;}
bool FRPGAuctionCoordinator::RegistrationAllowed() const
{
    // The receipt must first have been refreshed. A later items GET legitimately supersedes
    // that projection ticket; registration uses its fresh bag token/revisions, not old assets.
    return Scope.IsValid() && Current.bEnabled && Current.bAuthenticated && Current.bEligible && !bBusy && !bStorageBlocked
        && !ClearIntent.IsSet() && !Client.GetPending() && !Client.NeedsAssetRefresh() && Now>=NextAttempt && Transport->CanSend();
}
bool FRPGAuctionCoordinator::ReadRegistrationPolicy()
{
    const auto KeepAlive=AsShared();if(!ReadAllowed(false))return false;
    Review.Reset();RegistrationReview.Reset();RegistrationPolicy.Reset();RegistrationError=FText::GetEmpty();RegistrationPolicyState=ERPGAuctionReadState::Loading;bBusy=true;
    const uint64 Expected=++Generation;TWeakPtr<FRPGAuctionCoordinator> Weak=AsShared();
    auto Done=[Weak,Expected](FRPGAuctionHttpResult R)
    {
        const auto S=Weak.Pin();if(!S || S->Generation!=Expected || !S->Sync() || S->Generation!=Expected)return;
        S->bBusy=false;S->RegistrationPolicyState=ERPGAuctionReadState::Failed;FRPGAuctionRegistrationPolicy P;
        if(R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode==200 && RPGAuction::DecodeRegistrationPolicy(R.Body,P))
        {S->RegistrationPolicy=MoveTemp(P);S->PolicyReceived=S->Now;S->RegistrationPolicyState=ERPGAuctionReadState::Ready;}
        S->ReadDelay(R);S->Notify();
    };
    const bool Sent=Transport->SendRegistrationPolicy(Scope.Character,Done);if(!Sent && Generation==Expected && bBusy)Done({});Notify();return Sent;
}
bool FRPGAuctionCoordinator::ReloadRegistrationBag()
{
    const auto KeepAlive=AsShared();if(!Sync() || !RegistrationAllowed() || !RegistrationBagReload)return false;
    Review.Reset();RegistrationReview.Reset();RegistrationError=FText::GetEmpty();const bool Started=RegistrationBagReload();Notify();return Started;
}
bool FRPGAuctionCoordinator::ReviewRegistration(FGuid BagToken,FGuid Item,const FString& Quantity,const FString& Price,const FString& Seconds)
{
    const auto KeepAlive=AsShared();FRPGTradeBagSnapshot Bag;int64 Q=0,P=0,H=0;
    if(!Sync())return false;
    auto Fail=[this]{RegistrationError=NSLOCTEXT("RPGAuction","RegistrationInvalid","수량·가격·기간과 최신 가방/등록 조건을 다시 확인해 주세요.");Notify();return false;};
    if(Review.IsSet() || RegistrationReview.IsSet() || !RegistrationAllowed() || !RegistrationPolicyCurrent() || !ReadRegistrationBag(Bag) || Bag.Token!=BagToken
        || !FRPGTradeOfferDraft::ParseInteger(Quantity,Q) || Q<1 || Q>MAX_int32 || !FRPGTradeOfferDraft::ParseInteger(Price,P)
        || P<RegistrationPolicy->MinimumPrice || P>RegistrationPolicy->MaximumPrice || !FRPGTradeOfferDraft::ParseInteger(Seconds,H)
        || H<RegistrationPolicy->MinimumSeconds || H>RegistrationPolicy->MaximumSeconds)return Fail();
    const auto* Entry=Bag.Find(Item);if(!Entry || !Entry->bEligible || Entry->Label.IsEmpty() || Q>Entry->Quantity)return Fail();
    const double Offset=Now-PolicyReceived+H;
    if(Offset>(FDateTime::MaxValue().GetTicks()-RegistrationPolicy->ObservedAt.GetTicks())/double(ETimespan::TicksPerSecond))return Fail();
    FRegistrationReview R;R.Token=FGuid::NewGuid();R.BagToken=Bag.Token;R.Item=Item;R.Listing=FGuid::NewGuid();R.Revision=Entry->Revision;R.Available=Entry->Quantity;
    R.Quantity=static_cast<int32>(Q);R.Price=P;R.PolicyTag=RegistrationPolicy->Tag;R.Expires=RegistrationPolicy->ObservedAt+FTimespan::FromSeconds(Offset);
    const auto Fee=FText::FromString(FString::Printf(TEXT("%d.%02d"),RegistrationPolicy->FeeBasisPoints/100,RegistrationPolicy->FeeBasisPoints%100));
    R.Text=FText::Format(NSLOCTEXT("RPGAuction","RegistrationReview","경매 등록 확인\n{0} × {1}\n총 판매 가격: {2} 골드 · 판매 수수료: {3}%\n판매 종료: {4}\n등록하면 선택한 수량을 판매 보관함으로 옮깁니다. 서버가 물품/기간/등록 한도를 최종 확인합니다."),Entry->Label,FText::AsNumber(R.Quantity),FText::AsNumber(P),Fee,FText::AsDateTime(R.Expires));
    RegistrationError=FText::GetEmpty();RegistrationReview=MoveTemp(R);Notify();return true;
}
bool FRPGAuctionCoordinator::RegistrationConsentCurrent() const
{
    FRPGTradeBagSnapshot Bag;if(!RegistrationReview.IsSet() || !RegistrationAllowed() || !RegistrationPolicyCurrent() || !ReadRegistrationBag(Bag))return false;
    const auto& R=*RegistrationReview;const auto* Item=Bag.Find(R.Item);
    const double Remaining=(R.Expires.GetTicks()-RegistrationPolicy->ObservedAt.GetTicks())/double(ETimespan::TicksPerSecond)-(Now-PolicyReceived);
    return Remaining>0 && Bag.Token==R.BagToken && RegistrationPolicy->Tag==R.PolicyTag && Item && Item->bEligible && Item->Revision==R.Revision && Item->Quantity==R.Available;
}
bool FRPGAuctionCoordinator::ConfirmRegistration(FGuid Token)
{
    const auto KeepAlive=AsShared();if(!Sync() || !RegistrationReview.IsSet() || RegistrationReview->Token!=Token)return false;
    const auto ExpectedScope=Scope;
    if(!RegistrationAllowed() || !Reconcile() || !Sync() || !(Scope==ExpectedScope) || !RegistrationReview.IsSet() || RegistrationReview->Token!=Token || !RegistrationConsentCurrent())
    {DismissReview(Token);return false;}
    const auto R=*RegistrationReview;RegistrationReview.Reset();
    if(Client.PrepareRegister(R.Listing,R.Item,R.Revision,R.Quantity,R.Price,R.Expires,R.PolicyTag))return SaveAndSend();
    Notify();return false;
}
void FRPGAuctionCoordinator::RegistrationView(FRPGAuctionScreenView& V)
{
    V.bRegistrationVisible=Current.bEnabled;V.bCanReadPolicy=V.bCanPublic;V.bCanReloadBag=RegistrationAllowed() && !!RegistrationBagReload;
    FRPGTradeBagSnapshot Bag;const bool ValidBag=ReadRegistrationBag(Bag);
    if(ValidBag){V.BagToken=Bag.Token;for(const auto& Item:Bag.Items)if(Item.bEligible && !Item.Label.IsEmpty())V.BagItems.Add(Item);}
    V.bCanReviewRegistration=!V.bHasReview && RegistrationAllowed() && RegistrationPolicyCurrent() && ValidBag && !V.BagItems.IsEmpty();
    V.RegistrationStatus=RegistrationError.IsEmpty()?(ValidBag?NSLOCTEXT("RPGAuction","RegistrationChoose","가방 물품을 선택하고 수량·총 판매 가격·판매 시간을 입력하세요.")
        :NSLOCTEXT("RPGAuction","RegistrationReload","등록할 물품의 최신 가방을 불러오세요. 가방 정보는 잠시 후 만료됩니다.")):RegistrationError;
    V.PolicyText=NSLOCTEXT("RPGAuction","PolicyPrompt","먼저 서버 등록 조건을 불러오세요.");
    if(RegistrationPolicyState==ERPGAuctionReadState::Loading)V.PolicyText=NSLOCTEXT("RPGAuction","PolicyLoading","등록 조건을 확인하고 있습니다.");
    else if(RegistrationPolicyState==ERPGAuctionReadState::Failed)V.PolicyText=NSLOCTEXT("RPGAuction","PolicyFailed","등록 조건을 불러오지 못했습니다. 다시 확인해 주세요.");
    else if(RegistrationPolicy.IsSet())V.PolicyText=RegistrationPolicyCurrent()
        ?FText::Format(NSLOCTEXT("RPGAuction","PolicyReady","등록 재화: 골드 · 총 가격 {0}~{1} · 판매 기간 {2}~{3}초"),FText::AsNumber(RegistrationPolicy->MinimumPrice),FText::AsNumber(RegistrationPolicy->MaximumPrice),FText::AsNumber(RegistrationPolicy->MinimumSeconds),FText::AsNumber(RegistrationPolicy->MaximumSeconds))
        :NSLOCTEXT("RPGAuction","PolicyExpired","등록 조건이 오래됐거나 재화 표시를 확인할 수 없습니다. 조건을 다시 불러오세요.");
    if(RegistrationReview.IsSet()){V.ReviewToken=RegistrationReview->Token;V.ReviewText=RegistrationReview->Text;V.bCanConfirm=RegistrationConsentCurrent();}
}
