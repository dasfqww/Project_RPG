#include "Trading/RPGTradeCoordinator.h"
#include "HAL/PlatformTime.h"
#include "Trading/RPGTradeIntentJournal.h"

FRPGTradeCoordinator::FRPGTradeCoordinator(TSharedRef<IRPGTradeTransport> InTransport, TFunction<double()> InClock, FString InJournalRoot)
    : Clock(InClock ? MoveTemp(InClock) : TFunction<double()>([] { return FPlatformTime::Seconds(); })), Transport(MoveTemp(InTransport)), JournalRoot(MoveTemp(InJournalRoot)) {}
FRPGTradeCoordinator::~FRPGTradeCoordinator() { Transport->Cancel(); }
void FRPGTradeCoordinator::Notify() { auto Callback = OnChanged; if (Callback) Callback(); }
void FRPGTradeCoordinator::Reset()
{
    ++Operation; Transport->Cancel(); bBusy = false;
    Client.Reset(); Endpoint.Reset(); Account.Reset(); Character.Invalidate(); PendingAction.Reset();
    Retries = 0; NextAttempt = 0; LastResponse = ERPGTradeResponse::Ignored;
    InvalidatePreview();
    ClearTargets(); NextSearch = 0; TargetNow=0;
    ClearIntent.Reset(); bPersistenceBlocked=HasPersistence(); NextPersistenceAttempt=0;
}
bool FRPGTradeCoordinator::ReconcilePersistence()
{
    if(!HasPersistence())return true;
    UpdateTime();
    auto Block=[this]{bPersistenceBlocked=true;NextPersistenceAttempt=Now+2;return false;};
    FRPGTradeIntentScope Scope;
    if(!FRPGTradeIntentScope::Create(Endpoint,Account,Character,Scope))return Block();
    FRPGTradePendingIntent Saved;
    if(ClearIntent.IsSet())
    {
        if(!(ClearIntent->Scope==Scope))return Block();
        if(!FRPGTradeIntentJournal::Clear(JournalRoot,*ClearIntent)
            && FRPGTradeIntentJournal::Load(JournalRoot,Scope,Saved)!=ERPGTradeJournalRead::Missing)return Block();
        ClearIntent.Reset();bPersistenceBlocked=false;return true;
    }
    const auto Read=FRPGTradeIntentJournal::Load(JournalRoot,Scope,Saved);
    if(Client.GetPending())
    {
        FRPGTradePendingIntent Current;if(!Client.ExportPending(Current) || !(Current.Scope==Scope))return Block();
        if(Read==ERPGTradeJournalRead::Missing)
        {if(!FRPGTradeIntentJournal::Create(JournalRoot,Current))return Block();}
        else if(Read!=ERPGTradeJournalRead::Ready || Saved.Request.RequestId!=Current.Request.RequestId
            || Saved.Request.TradeId!=Current.Request.TradeId || Saved.Request.RelativePath!=Current.Request.RelativePath
            || Saved.Request.Body!=Current.Request.Body)return Block();
    }
    else if(Read==ERPGTradeJournalRead::Ready)
    {
        if(!Client.RestorePending(Saved))return Block();
        PendingAction=Saved.Request.RelativePath.Mid(Saved.Request.RelativePath.Find(TEXT("/"),ESearchCase::CaseSensitive,ESearchDir::FromEnd)+1);
        Retries=0;NextAttempt=Now;InvalidatePreview();ClearTargets();
    }
    else if(Read!=ERPGTradeJournalRead::Missing)return Block();
    bPersistenceBlocked=false;return true;
}
bool FRPGTradeCoordinator::SyncContext()
{
    FString CurrentEndpoint, CurrentAccount;
    if (!Character.IsValid()) return false;
    if (!Transport->GetContext(CurrentEndpoint, CurrentAccount) || CurrentEndpoint != Endpoint || CurrentAccount != Account)
    { Reset(); Notify(); return false; }
    return true;
}
bool FRPGTradeCoordinator::SelectCharacter(FGuid Selected)
{
    FString CurrentEndpoint, CurrentAccount;
    if (!Selected.IsValid() || !Transport->GetContext(CurrentEndpoint, CurrentAccount)) return false;
    if (Character == Selected && Endpoint == CurrentEndpoint && Account == CurrentAccount) return true;
    Reset();
    if (!Client.Bind(CurrentEndpoint, CurrentAccount, Selected)) return false;
    Character = Selected; Endpoint = CurrentEndpoint; Account = CurrentAccount;
    ReconcilePersistence();
    Notify(); return true;
}
bool FRPGTradeCoordinator::Refresh(FGuid Trade)
{
    if (!SyncContext() || IsRecoveryBlocked() || bBusy || !Transport->CanSend()) return false;
    const auto Ticket = Client.BeginRead(Trade);
    const auto Expected = ++Operation;
    bBusy = true;
    TWeakPtr<FRPGTradeCoordinator> WeakThis = AsShared();
    const bool bSent = Transport->Send(Character, Trade, TEXT(""), TEXT(""),
        [WeakThis, Ticket, Expected](FRPGTradeHttpResult Result)
        {
            auto Self = WeakThis.Pin();
            if (!Self || Self->Operation != Expected || !Self->SyncContext()) return;
            Self->bBusy = false;
            Self->LastResponse = Self->Client.CompleteRead(Ticket, Result.bCurrentPlayerContext && Result.bTransportSuccessful ? Result.StatusCode : 0, Result.Body);
            const auto* Current = Self->Client.GetSession();
            if (Current) Self->ClearTargets();
            if (Self->LastResponse != ERPGTradeResponse::Applied || !Current || (Self->Preview.IsSet() && !(Self->Preview->Session == *Current))) Self->InvalidatePreview();
            // A read cannot shorten a server-directed retry wait or resolve an uncertain write.
            Self->Notify();
        });
    if (!bSent && Operation == Expected)
    { bBusy = false; LastResponse = Client.CompleteRead(Ticket, 0, TEXT("")); Notify(); }
    return bSent;
}
void FRPGTradeCoordinator::InvalidatePreview()
{ Preview.Reset(); PreviewToken.Invalidate(); PreviewDeadline = 0; }
bool FRPGTradeCoordinator::RefreshAssets(TFunction<void(FRPGTradeHttpResult)> Completion)
{
    if(!Completion || !SyncContext() || IsRecoveryBlocked() || bBusy || !Transport->CanSend() || Client.GetPending()
        || !Client.GetSession() || Client.GetSession()->Status!=ERPGTradeStatus::Completed) return false;
    const auto Trade=Client.GetSession()->TradeId;const auto Expected=++Operation;bBusy=true;
    TWeakPtr<FRPGTradeCoordinator> Weak=AsShared();
    auto Done=[Weak,Expected,Completion=MoveTemp(Completion)](FRPGTradeHttpResult Result) mutable
    {auto Self=Weak.Pin();if(!Self || Self->Operation!=Expected || !Self->SyncContext())return;Self->bBusy=false;Completion(MoveTemp(Result));Self->Notify();};
    const bool Sent=Transport->SendAssets(Character,Trade,Done);
    if(!Sent && Operation==Expected && bBusy)Done(FRPGTradeHttpResult{});
    return Sent;
}
const FRPGTradePreview* FRPGTradeCoordinator::GetPreview() const
{
    const double RawTime = Clock();
    const auto* Session = Client.GetSession();
    return Preview.IsSet() && Session && !Client.GetPending() && Preview->Session == *Session
        && FMath::IsFinite(RawTime) && FMath::Max(Now,RawTime) < PreviewDeadline ? &Preview.GetValue() : nullptr;
}
bool FRPGTradeCoordinator::RefreshPreview()
{
    if (!SyncContext() || IsRecoveryBlocked() || bBusy || !Transport->CanSend() || Client.GetPending() || !Client.GetSession()) return false;
    UpdateTime();
    const auto Trade = Client.GetSession()->TradeId;
    const auto Ticket = Client.BeginRead(Trade);
    const auto Expected = ++Operation;
    const double Started = Now;
    bBusy = true;
    TWeakPtr<FRPGTradeCoordinator> WeakThis = AsShared();
    auto Completion = [WeakThis,Ticket,Expected,Started](FRPGTradeHttpResult Result)
    {
        auto Self = WeakThis.Pin();
        if (!Self || Self->Operation != Expected || !Self->SyncContext()) return;
        Self->bBusy = false; Self->UpdateTime();
        Self->LastResponse = Self->Client.CompleteRead(Ticket, Result.bCurrentPlayerContext && Result.bTransportSuccessful ? Result.StatusCode : 0, Result.Body,131072);
        const auto* Current = Self->Client.GetSession(); FRPGTradePreview Incoming;
        if (Self->LastResponse == ERPGTradeResponse::Applied && Current
            && FRPGTradePreview::Decode(Result.Body,Self->Character,*Current,Incoming))
        {
            double Window = FMath::Min(30.0,(Incoming.Session.ExpiresAt-Incoming.ObservedAt).GetTotalSeconds());
            auto BoundExpiry = [&](const TArray<FRPGTradePreviewItem>& Items)
            { for (const auto& Item : Items) if (Item.Details.IsSet() && Item.Details->ExpiresAt.IsSet()) Window = FMath::Min(Window,(Item.Details->ExpiresAt.GetValue()-Incoming.ObservedAt).GetTotalSeconds()); };
            BoundExpiry(Incoming.InitiatorItems); BoundExpiry(Incoming.InviteeItems);
            const bool Same = Self->Preview.IsSet() && Self->Preview->SameContent(Incoming);
            Self->Preview = MoveTemp(Incoming);
            if (!Same || !Self->PreviewToken.IsValid()) Self->PreviewToken = FGuid::NewGuid();
            Self->PreviewDeadline = Self->Now + FMath::Max(0.0,Window-FMath::Max(0.0,Self->Now-Started)-.1);
        }
        else Self->InvalidatePreview();
        Self->Notify();
    };
    const bool Sent = Transport->SendPreview(Character,Trade,Completion);
    if (!Sent && Operation == Expected && bBusy) Completion(FRPGTradeHttpResult{});
    return Sent;
}
bool FRPGTradeCoordinator::Invite(FGuid Invitee)
{
    const auto KeepAlive=AsShared();
    if (!SyncContext() || bBusy) return false;
    if(!ReconcilePersistence() || Client.GetPending()){Notify();return false;}
    if(!Client.PrepareInvite(Invitee))return false;
    InvalidatePreview();
    ClearTargets();
    PendingAction = TEXT("invite"); Retries = 0; NextAttempt = Now;
    ReconcilePersistence();Dispatch();Notify();return true;
}
bool FRPGTradeCoordinator::Submit(ERPGTradeAction Action, const FRPGTradeOffer* Offer)
{
    const auto KeepAlive=AsShared();
    if (!SyncContext() || bBusy) return false;
    if(!ReconcilePersistence() || Client.GetPending()){Notify();return false;}
    if(!Client.Prepare(Action, Offer))return false;
    InvalidatePreview();
    ClearTargets();
    switch (Action)
    {
    case ERPGTradeAction::Accept: PendingAction = TEXT("accept"); break;
    case ERPGTradeAction::Offer: PendingAction = TEXT("offer"); break;
    case ERPGTradeAction::Ready: PendingAction = TEXT("ready"); break;
    case ERPGTradeAction::Withdraw: PendingAction = TEXT("withdraw"); break;
    case ERPGTradeAction::Confirm: PendingAction = TEXT("confirm"); break;
    case ERPGTradeAction::Cancel: PendingAction = TEXT("cancel"); break;
    default: return false;
    }
    Retries = 0; NextAttempt = Now;
    ReconcilePersistence();Dispatch();Notify();return true;
}
void FRPGTradeCoordinator::ClearTargets()
{
    ++TargetGeneration; Targets.Reset(); TargetToken.Invalidate(); TargetDeadline=0; TargetState=ERPGTradeTargetState::Idle;
}
bool FRPGTradeCoordinator::CanSearchTargets() const
{
    const double Raw=Clock();
    return !IsRecoveryBlocked() && !bBusy && !Client.GetSession() && !Client.GetPending() && FMath::IsFinite(Raw) && Raw>=0
        && FMath::Max(Now,Raw)>=NextSearch && Transport->CanSend();
}
const TArray<FRPGTradeTarget>* FRPGTradeCoordinator::GetTargets() const
{
    const double Raw=Clock();
    if (!FMath::IsFinite(Raw) || Raw<0) return nullptr;
    TargetNow=FMath::Max(TargetNow,FMath::Max(Now,Raw));
    return (TargetState==ERPGTradeTargetState::Ready || TargetState==ERPGTradeTargetState::Empty)
        && !Client.GetSession() && !Client.GetPending() && FMath::IsFinite(Raw) && Raw>=0
        && TargetNow<TargetDeadline ? &Targets : nullptr;
}
bool FRPGTradeCoordinator::SearchTargets(const FString& Input)
{
    FString Name;
    if (!SyncContext() || !CanSearchTargets() || !RPGTradeTargets::NormalizeName(Input,Name)) return false;
    UpdateTime(); ClearTargets(); const auto Epoch=TargetGeneration;
    const double Started=Now; NextSearch=Now+2; TargetState=ERPGTradeTargetState::Loading;
    const auto Expected=++Operation; bBusy=true;
    TWeakPtr<FRPGTradeCoordinator> WeakThis=AsShared();
    auto Completion=[WeakThis,Expected,Epoch,Started,Name](FRPGTradeHttpResult Result)
    {
        auto Self=WeakThis.Pin();
        if (!Self || Self->Operation!=Expected || !Self->SyncContext()) return;
        Self->bBusy=false; Self->UpdateTime();
        if (Self->TargetGeneration==Epoch)
        {
            Self->TargetState=ERPGTradeTargetState::Failed;
            if (Result.bCurrentPlayerContext && Result.bTransportSuccessful)
            {
                if (Result.StatusCode==200 && RPGTradeTargets::Decode(Result.Body,Self->Character,Name,Self->Targets))
                {
                    Self->TargetState=Self->Targets.IsEmpty() ? ERPGTradeTargetState::Empty : ERPGTradeTargetState::Ready;
                    Self->TargetToken=FGuid::NewGuid();
                    Self->TargetDeadline=Self->Now+FMath::Max(0.0,29.9-FMath::Max(0.0,Self->Now-Started));
                }
                else if (Result.StatusCode==429 || Result.StatusCode==503)
                {
                    const double Delay=FMath::IsFinite(Result.RetryAfterSeconds) && Result.RetryAfterSeconds>0
                        ? FMath::Clamp(Result.RetryAfterSeconds,1.0,3600.0) : 60.0;
                    Self->NextSearch=Self->Now+Delay; Self->TargetState=ERPGTradeTargetState::Throttled;
                }
            }
        }
        Self->Notify();
    };
    const bool Sent=Transport->SendTargets(Character,Name,Completion);
    if (!Sent && Operation==Expected && bBusy) Completion(FRPGTradeHttpResult{});
    Notify(); return Sent;
}
bool FRPGTradeCoordinator::Dispatch()
{
    UpdateTime();
    if (!SyncContext() || IsRecoveryBlocked() || bBusy || !Transport->CanSend() || Now < NextAttempt || !Client.GetPending()) return false;
    if(!ReconcilePersistence()) {Notify();return false;}
    FRPGTradePendingIntent Original;
    if(HasPersistence() && !Client.ExportPending(Original))return false;
    FRPGTradeTicket Ticket;
    if (!Client.BeginAttempt(Ticket)) return false;
    const auto Request = *Client.GetPending();
    const auto Expected = ++Operation;
    bBusy = true;
    TWeakPtr<FRPGTradeCoordinator> WeakThis = AsShared();
    auto Completion = [WeakThis, Ticket, Expected, Original](FRPGTradeHttpResult Result)
    {
        auto Self = WeakThis.Pin();
        if (!Self || Self->Operation != Expected || !Self->SyncContext()) return;
        Self->bBusy = false;
        Self->LastResponse = Self->Client.CompleteAttempt(Ticket, Result.bCurrentPlayerContext && Result.bTransportSuccessful ? Result.StatusCode : 0, Result.Body);
        if (Self->Client.GetPending())
        {
            Self->UpdateTime();
            ++Self->Retries;
            const double Backoff = FMath::Min(30.0, FMath::Pow(2.0, FMath::Min(Self->Retries - 1, 5)));
            Self->NextAttempt = Self->Now + FMath::Max(Backoff, FMath::IsFinite(Result.RetryAfterSeconds) ? FMath::Clamp(Result.RetryAfterSeconds, 0.0, 3600.0) : 60.0);
        }
        else
        {
            Self->PendingAction.Reset(); Self->Retries = 0;
            if(Self->HasPersistence()){Self->ClearIntent=Original;Self->ReconcilePersistence();}
        }
        Self->Notify();
    };
    const bool bSent = Transport->Send(Character, Request.TradeId, PendingAction, Request.Body, Completion);
    if (!bSent && Operation == Expected && bBusy) Completion(FRPGTradeHttpResult{});
    return bSent;
}
bool FRPGTradeCoordinator::Retry()
{
    const auto KeepAlive=AsShared();
    UpdateTime();
    if (!SyncContext() || bBusy) return false;
    const bool WasBlocked=IsRecoveryBlocked();
    if(!ReconcilePersistence()){Notify();return false;}
    if(!Client.GetPending()){if(WasBlocked)Notify();return WasBlocked;}
    if(Now < NextAttempt)return false;
    Retries = 0;
    return Dispatch();
}
void FRPGTradeCoordinator::UpdateTime()
{
    const double NowSeconds = Clock();
    if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0) return;
    Now = FMath::Max(Now, NowSeconds);
}
void FRPGTradeCoordinator::Pump(bool bAllowDispatch)
{
    const auto KeepAlive=AsShared();
    UpdateTime();
    if(!SyncContext() || !bAllowDispatch)return;
    if(IsRecoveryBlocked())
    {
        if(Now<NextPersistenceAttempt)return;
        const auto Epoch=Operation;ReconcilePersistence();Notify();
        if(Epoch!=Operation || IsRecoveryBlocked())return;
    }
    if(Retries<6)Dispatch();
}
