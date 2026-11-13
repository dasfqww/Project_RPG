#include "Auction/RPGAuctionCoordinator.h"
#include "HAL/PlatformTime.h"
#include "Trading/RPGTradeJson.h"

namespace
{
bool Same(const FRPGAuctionIntent& A, const FRPGAuctionIntent& B)
{
    return A.Scope == B.Scope && A.Request.RequestId == B.Request.RequestId && A.Request.ListingId == B.Request.ListingId
        && A.Request.Action == B.Request.Action && A.Request.RelativePath == B.Request.RelativePath && A.Request.Body == B.Request.Body;
}
}
FRPGAuctionCoordinator::FRPGAuctionCoordinator(TSharedRef<IRPGAuctionTransport> InTransport, FContext InContext, FString Root, TFunction<double()> InClock)
    : Transport(MoveTemp(InTransport)), Context(MoveTemp(InContext)), JournalRoot(MoveTemp(Root)),
      Clock(InClock ? MoveTemp(InClock) : TFunction<double()>([] { return FPlatformTime::Seconds(); })) {}
FRPGAuctionCoordinator::~FRPGAuctionCoordinator() { Shutdown(); }
void FRPGAuctionCoordinator::UpdateTime()
{ const double T = Clock(); if (FMath::IsFinite(T) && T >= 0) Now = FMath::Max(Now, T); }
void FRPGAuctionCoordinator::Notify()
{ auto Callback = OnChanged; if (!bEnding && Callback) Callback(); if(!bEnding)OnViewChanged.Broadcast(); }
void FRPGAuctionCoordinator::ResetMemory()
{
    ++Generation; Transport->Cancel(); bBusy = false; Client.Reset(); Scope = {}; ClearIntent.Reset();
    bStorageBlocked = false; NextAttempt = NextStorage = 0; LastResponse = ERPGAuctionResponse::Ignored;
    Assets.Reset(); AssetsState=ERPGAuctionAssetsState::Idle;
    RegistrationPolicy.Reset();RegistrationPolicyState=ERPGAuctionReadState::Idle;PolicyReceived=0;RegistrationError=FText::GetEmpty();
    ClearBrowse();
}
void FRPGAuctionCoordinator::Shutdown()
{ if (bEnding) return; bEnding = true; OnChanged = {}; OnViewChanged.Clear(); ResetMemory(); }
bool FRPGAuctionCoordinator::Sync()
{
    if (bEnding || !Context) return false;
    UpdateTime();
    const auto Next = Context();
    const bool IdentityChanged = Current.Endpoint != Next.Endpoint || Current.Account != Next.Account;
    if (IdentityChanged)
    {
        if (!Current.Account.IsEmpty()) BlockedSelection = Next.Character;
        ResetMemory();
    }
    if (!Next.Character.IsValid()) BlockedSelection.Invalidate();
    Current = Next;
    FRPGTradeIntentScope Incoming;
    if (!Current.bLocalLobby || Current.Character == BlockedSelection
        || !FRPGTradeIntentScope::Create(Current.Endpoint, Current.Account, Current.Character, Incoming))
    {
        // Leaving the lobby cancels callbacks, but never deletes the original disk intent.
        if (Scope.IsValid()) ResetMemory();
        return false;
    }
    if (!(Scope == Incoming))
    {
        ResetMemory(); Scope = Incoming;
        if (!Client.Bind(Scope.Endpoint, Scope.Account, Scope.Character)) return false;
        Reconcile();
    }
    FString Endpoint, Account;
    FRPGTradeIntentScope TransportScope;
    if (!Transport->GetContext(Endpoint, Account) || !FRPGTradeIntentScope::Create(Endpoint, Account, Scope.Character, TransportScope)
        || !(TransportScope == Scope)) { ResetMemory(); return false; }
    return true;
}
bool FRPGAuctionCoordinator::Reconcile()
{
    auto Block = [this] { bStorageBlocked = true; NextStorage = Now + 2; return false; };
    if (!Scope.IsValid() || JournalRoot.IsEmpty()) return Block();
    FRPGAuctionIntent Saved;
    if (ClearIntent.IsSet())
    {
        if (!(ClearIntent->Scope == Scope)) return Block();
        if (!FRPGAuctionIntentJournal::Clear(JournalRoot, *ClearIntent)
            && FRPGAuctionIntentJournal::Load(JournalRoot, Scope, Saved) != ERPGAuctionJournalRead::Missing) return Block();
        ClearIntent.Reset(); bStorageBlocked = false; return true;
    }
    const auto Read = FRPGAuctionIntentJournal::Load(JournalRoot, Scope, Saved);
    if (Client.GetPending())
    {
        FRPGAuctionIntent Original;
        if (!Client.ExportPending(Original)) return Block();
        if (Read == ERPGAuctionJournalRead::Missing)
        { if (!FRPGAuctionIntentJournal::Create(JournalRoot, Original)) return Block(); }
        else if (Read != ERPGAuctionJournalRead::Ready || !Same(Original, Saved)) return Block();
    }
    else if (Read == ERPGAuctionJournalRead::Ready)
    { if (!Client.RestorePending(Saved)) return Block(); }
    else if (Read != ERPGAuctionJournalRead::Missing) return Block();
    bStorageBlocked = false; return true;
}
void FRPGAuctionCoordinator::Tick()
{
    const auto KeepAlive = AsShared(); const uint64 Previous = Generation; const bool Blocked = bStorageBlocked;
    if (Sync() && bStorageBlocked && !bBusy && Now >= NextStorage) Reconcile();
    if (Previous != Generation || Blocked != bStorageBlocked) Notify();
    else if(!bEnding)OnViewChanged.Broadcast(); // Local view deadlines/auth state; never polls HTTP.
}
bool FRPGAuctionCoordinator::BlocksTransitions()
{
    if (!Sync()) return false;
    return bBusy || bStorageBlocked || Client.GetPending() || ClearIntent.IsSet();
}
bool FRPGAuctionCoordinator::CanPrepare(bool bNewSale)
{
    return Sync() && Current.bAuthenticated && Current.bEligible && (!bNewSale || Current.bEnabled)
        && !bBusy && Reconcile() && !Client.GetPending() && !Client.NeedsAssetRefresh()
        && (!Client.GetReceipt() || !AssetCurrent || (AssetsState==ERPGAuctionAssetsState::Ready && Assets.IsSet() && AssetCurrent()));
}
bool FRPGAuctionCoordinator::SaveAndSend()
{
    // True means an in-memory intent was prepared; a storage failure blocks all dispatch/replacement.
    // The public result below is true only after a durable write succeeded.
    if (!Reconcile()) { Notify(); return false; }
    Assets.Reset(); AssetsState=ERPGAuctionAssetsState::Idle;
    ClearBrowse();
    Dispatch(false); Notify(); return true;
}
bool FRPGAuctionCoordinator::Register(FGuid L, FGuid I, int64 R, int32 Q, int64 P, FDateTime E)
{ const auto KeepAlive = AsShared(); return CanPrepare(true) && Client.PrepareRegister(L, I, R, Q, P, E) && SaveAndSend(); }
bool FRPGAuctionCoordinator::Purchase(const FRPGAuctionListing& L)
{ const auto KeepAlive = AsShared(); return CanPrepare(true) && Client.PreparePurchase(L) && SaveAndSend(); }
bool FRPGAuctionCoordinator::CancelListing(const FRPGAuctionListing& L)
{ const auto KeepAlive = AsShared(); return CanPrepare(false) && Client.PrepareCancel(L) && SaveAndSend(); }
bool FRPGAuctionCoordinator::Claim(const FRPGAuctionPayable& P, int64 Amount)
{ const auto KeepAlive = AsShared(); return CanPrepare(false) && Client.PrepareClaim(P, Amount) && SaveAndSend(); }
bool FRPGAuctionCoordinator::Retry()
{ const auto KeepAlive = AsShared(); const bool Sent = Dispatch(false); Notify(); return Sent; }
bool FRPGAuctionCoordinator::Recover()
{ const auto KeepAlive = AsShared(); const bool Sent = Dispatch(true); Notify(); return Sent; }
bool FRPGAuctionCoordinator::Dispatch(bool bRecovery)
{
    if (!Sync() || !Current.bAuthenticated || bBusy || Now < NextAttempt || !Reconcile()
        || !Client.GetPending() || !Transport->CanSend()) return false;
    FRPGAuctionIntent Original; if (!Client.ExportPending(Original)) return false;
    FRPGTradeTicket Ticket; FString Path;
    if (!(bRecovery ? Client.BeginRecovery(Ticket, Path) : Client.BeginAttempt(Ticket))) return false;
    const uint64 Expected = ++Generation; bBusy = true;
    TWeakPtr<FRPGAuctionCoordinator> Weak = AsShared();
    auto Done = [Weak, Expected, Ticket, Original, bRecovery](FRPGAuctionHttpResult Result)
    {
        auto Self = Weak.Pin();
        if (!Self || Self->Generation != Expected || !Self->Sync() || Self->Generation != Expected) return;
        Self->bBusy = false;
        const int32 Code = Result.bCurrentPlayerContext && Result.bTransportSuccessful ? Result.StatusCode : 0;
        const FString Body = Code ? Result.Body : FString();
        Self->LastResponse = bRecovery ? Self->Client.CompleteRecovery(Ticket, Code, Body) : Self->Client.CompleteAttempt(Ticket, Code, Body);
        Self->UpdateTime();
        if (!Self->Client.GetPending())
        { Self->ClearIntent = Original; Self->Reconcile(); }
        else
        {
            const double Delay = (Code == 429 || Code == 503)
                ? (FMath::IsFinite(Result.RetryAfterSeconds) && Result.RetryAfterSeconds > 0 ? FMath::Clamp(Result.RetryAfterSeconds, 1.0, 3600.0) : 60.0) : 1.0;
            Self->NextAttempt = Self->Now + Delay;
        }
        Self->Notify();
    };
    const bool Sent = bRecovery ? Transport->SendReceipt(Scope.Character, Original.Request.RequestId, Done) : Transport->SendIntent(Original, Done);
    if (!Sent && Generation == Expected && bBusy) Done(FRPGAuctionHttpResult{});
    return Sent;
}
bool FRPGAuctionCoordinator::AcknowledgeAssets(FGuid Request)
{
    if (!Sync() || bBusy || bStorageBlocked || Client.GetPending() || !Client.AcknowledgeAssets(Request)) return false;
    Notify(); return true;
}

void FRPGAuctionCoordinator::SetAssetSink(TFunction<bool()> Begin,TFunction<bool(const FRPGTradeAssets&)> Apply,TFunction<bool()> CurrentRead)
{
    if(AssetsState==ERPGAuctionAssetsState::Loading){++Generation;Transport->Cancel();bBusy=false;}
    AssetBegin=MoveTemp(Begin);AssetApply=MoveTemp(Apply);AssetCurrent=MoveTemp(CurrentRead);Assets.Reset();AssetsState=ERPGAuctionAssetsState::Idle;
}
const FRPGTradeAssets* FRPGAuctionCoordinator::GetAssets() const
{
    if(bEnding || !Context || !Assets.IsSet() || AssetsState!=ERPGAuctionAssetsState::Ready || !AssetCurrent)return nullptr;
    const auto Next=Context();FRPGTradeIntentScope Viewed;
    return Next.bLocalLobby && Next.bAuthenticated && Next.bEligible
        && FRPGTradeIntentScope::Create(Next.Endpoint,Next.Account,Next.Character,Viewed) && Viewed==Scope
        && AssetCurrent() ? &Assets.GetValue() : nullptr;
}
bool FRPGAuctionCoordinator::ReloadAssets()
{
    const auto KeepAlive=AsShared();
    if(!Sync() || bBusy || bStorageBlocked || !Current.bAuthenticated || !Current.bEligible || Now<NextAttempt
        || Client.GetPending() || !Client.GetReceipt() || !AssetBegin || !AssetApply || !AssetCurrent || !Transport->CanSend())return false;
    const auto Receipt=*Client.GetReceipt();const auto Captured=Scope;const uint64 Expected=++Generation;
    bBusy=true;Assets.Reset();AssetsState=ERPGAuctionAssetsState::Loading;
    const bool Began=AssetBegin();
    if(!Sync() || Generation!=Expected)return false;
    if(!Began){bBusy=false;AssetsState=ERPGAuctionAssetsState::Failed;Notify();return false;}
    TWeakPtr<FRPGAuctionCoordinator> Weak=AsShared();
    auto Done=[Weak,Receipt,Captured,Expected](FRPGAuctionHttpResult Result)
    {
        auto Self=Weak.Pin();if(!Self || Self->Generation!=Expected || !Self->Sync() || Self->Generation!=Expected)return;
        const int32 Code=Result.bCurrentPlayerContext && Result.bTransportSuccessful ? Result.StatusCode : 0;
        RPGTradeClient::FObject Obj;FGuid Request;FDateTime Observed;FRPGTradeAssets Incoming;
        bool Valid=Code==200 && RPGTradeClient::ReadObject(Result.Body,Obj,4*1024*1024)
            && RPGTradeClient::Guid(Obj,TEXT("requestId"),Request) && Request==Receipt.RequestId
            && RPGTradeClient::Date(Obj,TEXT("observedAt"),Observed)
            && FRPGTradeAssets::DecodeOwned(Result.Body,Captured.Character,Captured.Account,Incoming);
        Valid=Valid && Self->AssetCurrent && Self->AssetCurrent() && Self->AssetApply && Self->AssetApply(Incoming) && Self->AssetCurrent();
        if(!Self->Sync() || Self->Generation!=Expected)return;
        Self->bBusy=false;Self->AssetsState=ERPGAuctionAssetsState::Failed;
        if(Valid && Self->Client.GetReceipt() && Self->Client.GetReceipt()->RequestId==Receipt.RequestId
            && Self->Client.AcknowledgeAssets(Receipt.RequestId))
        {Self->Assets=MoveTemp(Incoming);Self->AssetsState=ERPGAuctionAssetsState::Ready;}
        if(Code==429 || Code==503)Self->NextAttempt=Self->Now+(FMath::IsFinite(Result.RetryAfterSeconds)&&Result.RetryAfterSeconds>0?FMath::Clamp(Result.RetryAfterSeconds,1.0,3600.0):60.0);
        Self->Notify();
    };
    const bool Sent=Transport->SendAssets(Scope.Character,Receipt.RequestId,Done);
    if(!Sent && Generation==Expected && bBusy)Done(FRPGAuctionHttpResult{});
    Notify();return Sent;
}
