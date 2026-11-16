#include "Shop/RPGShopCoordinator.h"
#include "Shop/RPGShopJournal.h"
#include "Trading/RPGTradeJson.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"

namespace
{
FText CurrencyName(const FString& Code)
{return Code==TEXT("Gold")||Code==TEXT("RosterGold")?NSLOCTEXT("RPGShop","Gold","골드"):NSLOCTEXT("RPGShop","Currency","재화");}
}

FRPGShopCoordinator::FRPGShopCoordinator(TSharedRef<IRPGShopTransport> T,FContext C,FString Root,TFunction<double()> InClock)
    :Transport(MoveTemp(T)),ReadContext(MoveTemp(C)),JournalRoot(MoveTemp(Root)),Clock(MoveTemp(InClock)){}
FRPGShopCoordinator::~FRPGShopCoordinator(){Shutdown();}
void FRPGShopCoordinator::Shutdown()
{if(bEnding)return;bEnding=true;++Generation;Transport->Cancel();bBusy=false;SaleReview.Reset();ReviewToken.Invalidate();}
void FRPGShopCoordinator::Notify(){OnViewChanged.Broadcast();}
bool FRPGShopCoordinator::Load()
{
    FRPGShopIntent I;const auto State=RPGShopJournal::Load(JournalRoot,Scope,I);
    bStorageBlocked=State!=ERPGShopJournal::Missing&&State!=ERPGShopJournal::Ready;
    if(State==ERPGShopJournal::Ready){Pending=MoveTemp(I);bUncertain=true;Status=NSLOCTEXT("RPGShop","RecoveredIntent","이전 거래의 결과를 확인해 주세요.");}
    if(bStorageBlocked)Status=NSLOCTEXT("RPGShop","StorageBlocked","거래 기록을 읽을 수 없습니다. 저장 상태를 확인한 뒤 다시 시도하세요.");
    return !bStorageBlocked;
}
bool FRPGShopCoordinator::Sync()
{
    if(bEnding||!ReadContext)return false;
    const double Time=Clock?Clock():FPlatformTime::Seconds();if(FMath::IsFinite(Time))Now=FMath::Max(Now,Time);
    const auto Previous=Current;Current=ReadContext();FRPGTradeIntentScope Incoming,TransportScope;FString Endpoint,Account;
    const bool Valid=FRPGTradeIntentScope::Create(Current.Endpoint,Current.Account,Current.Character,Incoming)
        &&Transport->GetContext(Endpoint,Account)&&FRPGTradeIntentScope::Create(Endpoint,Account,Current.Character,TransportScope)&&Incoming==TransportScope;
    if(!Valid||(Scope.IsValid()&&!(Scope==Incoming)&&(Pending.IsSet()||bStorageBlocked)))
    {
        if(!bContextMismatch){++Generation;Transport->Cancel();bBusy=false;Snapshot.Reset();SnapshotToken.Invalidate();SaleReview.Reset();ReviewToken.Invalidate();if(Pending)bUncertain=true;}
        bContextMismatch=true;return false;
    }
    if(!(Scope==Incoming))
    {
        ++Generation;Transport->Cancel();bBusy=false;Scope=Incoming;Snapshot.Reset();SnapshotToken.Invalidate();SaleReview.Reset();ReviewToken.Invalidate();Receipt.Reset();Pending.Reset();PendingName=FText::GetEmpty();NextAttempt=0;Status=FText::GetEmpty();Load();
    }
    if((Previous.bAuthenticated&&!Current.bAuthenticated)||(Previous.bLocalLobby&&!Current.bLocalLobby))
    {++Generation;Transport->Cancel();bBusy=false;SaleReview.Reset();ReviewToken.Invalidate();if(Pending)bUncertain=true;}
    bContextMismatch=false;return true;
}
void FRPGShopCoordinator::Tick()
{
    const bool Before=bContextMismatch;const auto BeforeScope=Scope;const bool Auth=Current.bAuthenticated;const bool Valid=Sync();
    if(SaleReview&&(!Valid||!Current.bEligible||Now-SnapshotAt>60)){SaleReview.Reset();ReviewToken.Invalidate();Notify();}
    else if(Before!=bContextMismatch||!(BeforeScope==Scope)||Auth!=Current.bAuthenticated)Notify();
}
bool FRPGShopCoordinator::BlocksTransitions(){Sync();return Pending.IsSet()||bStorageBlocked;}
bool FRPGShopCoordinator::Ready() const
{return !bEnding&&!bContextMismatch&&!bBusy&&!bStorageBlocked&&Current.bAuthenticated&&Current.bLocalLobby&&Current.bEligible&&Now>=NextAttempt&&Transport->CanSend();}
bool FRPGShopCoordinator::NewCommandAllowed() const
{return Ready()&&!Pending&&Snapshot.IsSet()&&SnapshotToken.IsValid()&&Now-SnapshotAt<=60;}
void FRPGShopCoordinator::Delay(const FRPGShopHttpResult& R)
{if(R.StatusCode==429||R.StatusCode==503)NextAttempt=Now+(FMath::IsFinite(R.RetryAfterSeconds)&&R.RetryAfterSeconds>0?FMath::Clamp(R.RetryAfterSeconds,1.0,3600.0):60.0);}
bool FRPGShopCoordinator::Refresh()
{
    const auto KeepAlive=AsShared();if(!Sync()||!Ready())return false;
    SaleReview.Reset();ReviewToken.Invalidate();SnapshotToken.Invalidate();bBusy=true;Status=NSLOCTEXT("RPGShop","Loading","상점 정보를 불러오는 중입니다.");
    const uint64 Ticket=++Generation;TWeakPtr<FRPGShopCoordinator> Weak=AsShared();
    auto Done=[Weak,Ticket](FRPGShopHttpResult R)
    {
        const auto S=Weak.Pin();if(!S||S->Generation!=Ticket||!S->Sync()||S->Generation!=Ticket)return;S->bBusy=false;S->Delay(R);FRPGShopSnapshot Incoming;
        if(R.bCurrentPlayerContext&&R.bTransportSuccessful&&R.StatusCode==200&&RPGShopCodec::Snapshot(R.Body,Incoming))
        {S->Snapshot=MoveTemp(Incoming);S->SnapshotToken=FGuid::NewGuid();S->SnapshotAt=S->Now;S->Status=NSLOCTEXT("RPGShop","Ready","거래할 물품을 선택하세요.");}
        else{RPGTradeClient::FObject O;FString Code;if(RPGTradeClient::ReadObject(R.Body,O,4096)&&RPGTradeClient::String(O,TEXT("error"),Code))S->Status=RPGShopCodec::Error(Code);else S->Status=NSLOCTEXT("RPGShop","ReadFailed","상점 정보를 불러오지 못했습니다. 다시 불러오세요.");}
        S->Notify();
    };
    const bool Sent=Transport->ReadShop(Scope.Character,Done);if(!Sent&&bBusy&&Generation==Ticket)Done(FRPGShopHttpResult{});Notify();return Sent;
}
bool FRPGShopCoordinator::Buy(FGuid Token,const FString& OfferId,int32 Quantity)
{
    if(!Sync()||!NewCommandAllowed()||Token!=SnapshotToken||Quantity<=0)return false;
    const auto* Offer=Snapshot->Offers.FindByPredicate([&](const FRPGShopOffer& O){return O.OfferId==OfferId;});
    if(!Offer||Quantity>Offer->MaximumQuantity||Quantity>Offer->MaximumPurchasable||Offer->UnitPrice>MAX_int64/Quantity||Offer->UnitPrice*Quantity>Snapshot->AvailableBalance)return false;
    FRPGShopIntent I;I.Scope=Scope;I.RequestId=FGuid::NewGuid();I.ShopId=Snapshot->ShopId;I.PolicyTag=Snapshot->PolicyTag;I.Action=TEXT("buy");I.OfferId=OfferId;I.CurrencyCode=Snapshot->CurrencyCode;I.Quantity=Quantity;I.UnitPrice=Offer->UnitPrice;I.Body=RPGShopCodec::Body(I);return Prepare(MoveTemp(I));
}
bool FRPGShopCoordinator::ReviewSale(FGuid Token,FGuid Item,int32 Quantity)
{
    if(!Sync()||!NewCommandAllowed()||Token!=SnapshotToken||Quantity<=0)return false;
    const auto* Row=Snapshot->SaleItems.FindByPredicate([&](const FRPGShopSaleItem& R){return R.ItemId==Item;});
    if(!Row||!Row->bCanSell||Quantity>Row->Quantity||Quantity>Row->MaximumQuantity||Row->UnitPrice>MAX_int64/Quantity||Snapshot->Balance>MAX_int64-Row->UnitPrice*Quantity)return false;
    FRPGShopIntent I;I.Scope=Scope;I.RequestId=FGuid::NewGuid();I.ShopId=Snapshot->ShopId;I.PolicyTag=Snapshot->PolicyTag;I.Action=TEXT("sell");I.OfferId=Row->RuleId;I.CurrencyCode=Snapshot->CurrencyCode;I.Quantity=Quantity;I.UnitPrice=Row->UnitPrice;I.ItemId=Item;I.ItemRevision=Row->Revision;I.Body=RPGShopCodec::Body(I);
    if(!I.IsValid())return false;SaleReview=MoveTemp(I);ReviewToken=FGuid::NewGuid();Notify();return true;
}
bool FRPGShopCoordinator::ConfirmSale(FGuid Token)
{
    if(!Sync()||!NewCommandAllowed()||!SaleReview||!Token.IsValid()||Token!=ReviewToken||!(SaleReview->Scope==Scope))return false;
    return Prepare(*SaleReview);
}
void FRPGShopCoordinator::DismissSale(){SaleReview.Reset();ReviewToken.Invalidate();Notify();}
bool FRPGShopCoordinator::Prepare(FRPGShopIntent I)
{
    const auto KeepAlive=AsShared();if(!I.IsValid()||!NewCommandAllowed()||!(I.Scope==Scope))return false;
    if(!RPGShopJournal::Create(JournalRoot,I)){bStorageBlocked=true;Status=NSLOCTEXT("RPGShop","SaveFailed","거래 기록을 저장하지 못했습니다. 저장 상태를 확인하세요.");Notify();return false;}
    PendingName=FText::GetEmpty();
    if(I.Action==TEXT("buy")){const auto* Row=Snapshot->Offers.FindByPredicate([&](const FRPGShopOffer& R){return R.OfferId==I.OfferId;});if(Row)PendingName=Row->DisplayName;}
    else{const auto* Row=Snapshot->SaleItems.FindByPredicate([&](const FRPGShopSaleItem& R){return R.ItemId==I.ItemId;});if(Row)PendingName=Row->DisplayName;}
    Pending=MoveTemp(I);Receipt.Reset();bUncertain=false;SaleReview.Reset();ReviewToken.Invalidate();SnapshotToken.Invalidate();Notify();
    // A listener may close/logout/shut down here; the saved original remains recoverable.
    if(!Sync()||!Ready())return true;Dispatch(false);return true;
}
bool FRPGShopCoordinator::ClearPending()
{
    if(!Pending)return true;
    if(!RPGShopJournal::Clear(JournalRoot,*Pending)){bStorageBlocked=true;Status=NSLOCTEXT("RPGShop","ClearFailed","거래 결과를 확인했지만 기록 정리를 마치지 못했습니다. 저장 상태를 확인하세요.");return false;}
    Pending.Reset();Receipt.Reset();PendingName=FText::GetEmpty();bUncertain=false;bStorageBlocked=false;return true;
}
bool FRPGShopCoordinator::Dispatch(bool Recovery)
{
    const auto KeepAlive=AsShared();if(!Sync()||!Ready()||!Pending||Receipt)return false;
    const auto I=*Pending;bBusy=true;
    Status=Recovery?NSLOCTEXT("RPGShop","Checking","거래 결과를 확인하고 있습니다."):NSLOCTEXT("RPGShop","Processing","거래를 처리하고 있습니다.");
    const uint64 Ticket=++Generation;TWeakPtr<FRPGShopCoordinator> Weak=AsShared();
    auto Done=[Weak,Ticket,I](FRPGShopHttpResult R)
    {
        const auto S=Weak.Pin();if(!S||S->Generation!=Ticket||!S->Sync()||S->Generation!=Ticket||!S->Pending||!S->Pending->Same(I))return;
        S->bBusy=false;S->Delay(R);const int32 Code=R.bCurrentPlayerContext&&R.bTransportSuccessful?R.StatusCode:0;FRPGShopReceipt Receipt;FString Error;
        if(Code==200&&RPGShopCodec::Receipt(R.Body,I,Receipt))
        {
            S->Receipt=Receipt;S->bUncertain=false;S->Status=NSLOCTEXT("RPGShop","Committed","거래 완료 · 가방 갱신 중");S->Notify();
            if(!S->bEnding&&S->Generation==Ticket)S->RefreshAssets();return;
        }
        // Only a stored terminal rejection for this exact command resolves an uncertain submission.
        if(Code==409&&RPGShopCodec::Rejection(R.Body,I,Error))
        {S->Status=RPGShopCodec::Error(Error);S->ClearPending();}
        else{S->bUncertain=true;S->Status=NSLOCTEXT("RPGShop","Unknown","거래 결과가 아직 확인되지 않았습니다. 결과 확인 또는 같은 거래 다시 시도를 이용하세요.");}
        S->Notify();
    };
    const bool Sent=Recovery?Transport->ReadReceipt(I,Done):Transport->SendIntent(I,Done);
    if(!Sent&&bBusy&&Generation==Ticket)Done(FRPGShopHttpResult{});Notify();return Sent;
}
bool FRPGShopCoordinator::Retry(){return Dispatch(false);}
bool FRPGShopCoordinator::Recover(){return Dispatch(true);}
void FRPGShopCoordinator::SetAssetSink(TFunction<bool()> Begin,TFunction<bool(const FRPGTradeAssets&)> Apply,TFunction<bool()> IsCurrent)
{AssetBegin=MoveTemp(Begin);AssetApply=MoveTemp(Apply);AssetCurrent=MoveTemp(IsCurrent);}
bool FRPGShopCoordinator::RefreshAssets()
{
    const auto KeepAlive=AsShared();if(!Sync()||!Ready()||!Pending||!Receipt||!AssetBegin||!AssetApply||!AssetCurrent)return false;
    const auto I=*Pending;const uint64 Ticket=++Generation;bBusy=true;const bool Began=AssetBegin();
    if(!Sync()||Generation!=Ticket)return false;
    if(!Began){bBusy=false;Status=NSLOCTEXT("RPGShop","AssetBusy","거래는 완료되었습니다. 가방 갱신을 다시 시도하세요.");Notify();return false;}
    TWeakPtr<FRPGShopCoordinator> Weak=AsShared();
    auto Done=[Weak,Ticket,I](FRPGShopHttpResult R)
    {
        const auto S=Weak.Pin();if(!S||S->Generation!=Ticket||!S->Sync()||S->Generation!=Ticket||!S->Pending||!S->Pending->Same(I)||!S->Receipt)return;
        RPGTradeClient::FObject O;FGuid Request;FRPGTradeAssets Assets;
        bool Valid=R.bCurrentPlayerContext&&R.bTransportSuccessful&&R.StatusCode==200&&RPGTradeClient::ReadObject(R.Body,O,4*1024*1024)
            &&RPGTradeClient::Guid(O,TEXT("requestId"),Request)&&Request==I.RequestId&&FRPGTradeAssets::DecodeOwned(R.Body,I.Scope.Character,I.Scope.Account,Assets);
        Valid=Valid&&S->AssetCurrent&&S->AssetCurrent()&&S->AssetApply&&S->AssetApply(Assets)&&S->AssetCurrent();
        if(!S->Sync()||S->Generation!=Ticket)return;S->bBusy=false;S->Delay(R);
        if(Valid)
        {S->Status=NSLOCTEXT("RPGShop","Complete","거래가 완료되어 가방과 재화에 반영되었습니다.");const bool Cleared=S->ClearPending();S->Notify();if(Cleared&&!S->bEnding&&S->Generation==Ticket)S->Refresh();}
        else{S->Status=NSLOCTEXT("RPGShop","AssetFailed","거래 완료 · 가방 갱신을 다시 시도하세요.");S->Notify();}
    };
    const bool Sent=Transport->ReadAssets(I,Done);if(!Sent&&bBusy&&Generation==Ticket)Done(FRPGShopHttpResult{});Notify();return Sent;
}
bool FRPGShopCoordinator::RecheckStorage()
{
    const auto KeepAlive=AsShared();if(!Sync()||bBusy||!bStorageBlocked)return false;
    FRPGShopIntent I;const auto State=RPGShopJournal::Load(JournalRoot,Scope,I);
    if(State==ERPGShopJournal::Ready)
    {
        if(Pending&&!Pending->Same(I))return false;
        Pending=MoveTemp(I);bStorageBlocked=false;if(!Receipt)bUncertain=true;
        Status=NSLOCTEXT("RPGShop","StorageReady","거래 기록을 확인했습니다. 원래 거래 결과를 확인해 주세요.");Notify();return true;
    }
    if(State==ERPGShopJournal::Missing&&!Pending){bStorageBlocked=false;Notify();return true;}
    return false;
}
FRPGShopScreenView FRPGShopCoordinator::GetView()
{
    const bool Valid=Sync();FRPGShopScreenView V;V.Context=Scope;V.bAvailable=Valid&&Current.bLocalLobby;V.Status=Status;
    V.bBusy=bBusy;V.bStorageBlocked=bStorageBlocked;V.bHasPending=Pending.IsSet();V.bHasSnapshot=Snapshot.IsSet();V.SnapshotToken=SnapshotToken;
    if(Snapshot)V.Snapshot=*Snapshot;
    V.bCanTrade=NewCommandAllowed();V.bCanRefresh=Ready();V.bCanRecover=Ready()&&Pending.IsSet()&&!Receipt;V.bCanRetry=V.bCanRecover;
    V.bCanAssets=Ready()&&Pending.IsSet()&&Receipt.IsSet();V.bCanStorage=Valid&&bStorageBlocked&&!bBusy;
    V.bHasReview=SaleReview.IsSet();V.ReviewToken=ReviewToken;V.bCanConfirm=V.bCanTrade&&V.bHasReview;
    if(SaleReview)V.ReviewText=FText::Format(NSLOCTEXT("RPGShop","Review","{0}개를 판매하고 {1} {2}을 받습니다. 판매한 물품은 재매입할 수 없습니다."),FText::AsNumber(SaleReview->Quantity),FText::AsNumber(SaleReview->UnitPrice*SaleReview->Quantity),CurrencyName(SaleReview->CurrencyCode));
    if(Pending)V.PendingText=FText::Format(NSLOCTEXT("RPGShop","Pending","{0} · {1} · {2}개 · {3} {4}\n{5}"),
        Pending->Action==TEXT("buy")?NSLOCTEXT("RPGShop","Buying","구매"):NSLOCTEXT("RPGShop","Selling","판매"),PendingName.IsEmpty()?NSLOCTEXT("RPGShop","OriginalItem","선택한 물품"):PendingName,FText::AsNumber(Pending->Quantity),FText::AsNumber(Pending->UnitPrice*Pending->Quantity),CurrencyName(Pending->CurrencyCode),
        Receipt?NSLOCTEXT("RPGShop","PendingAssets","거래 완료 · 가방 갱신 필요"):NSLOCTEXT("RPGShop","PendingResult","거래 결과 확인 중"));
    if(bContextMismatch)V.Status=NSLOCTEXT("RPGShop","ContextChanged","거래를 시작한 계정과 캐릭터로 돌아가 결과를 확인하세요.");
    else if(Snapshot&&!Pending&&!bBusy&&!bStorageBlocked&&SnapshotToken.IsValid()&&Now-SnapshotAt>60)V.Status=NSLOCTEXT("RPGShop","Stale","상점 정보가 오래되었습니다. 새로고침해 주세요.");
    return V;
}
