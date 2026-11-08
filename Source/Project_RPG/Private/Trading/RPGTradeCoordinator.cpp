#include "Trading/RPGTradeCoordinator.h"
#include "HAL/PlatformTime.h"

FRPGTradeCoordinator::FRPGTradeCoordinator(TSharedRef<IRPGTradeTransport> InTransport, TFunction<double()> InClock)
    : Clock(InClock ? MoveTemp(InClock) : TFunction<double()>([] { return FPlatformTime::Seconds(); })), Transport(MoveTemp(InTransport)) {}
FRPGTradeCoordinator::~FRPGTradeCoordinator() { Transport->Cancel(); }
void FRPGTradeCoordinator::Notify() { auto Callback = OnChanged; if (Callback) Callback(); }
void FRPGTradeCoordinator::Reset()
{
    ++Operation; Transport->Cancel(); bBusy = false;
    Client.Reset(); Endpoint.Reset(); Account.Reset(); Character.Invalidate(); PendingAction.Reset();
    Retries = 0; NextAttempt = 0; LastResponse = ERPGTradeResponse::Ignored;
    InvalidatePreview();
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
    Notify(); return true;
}
bool FRPGTradeCoordinator::Refresh(FGuid Trade)
{
    if (!SyncContext() || bBusy || !Transport->CanSend()) return false;
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
const FRPGTradePreview* FRPGTradeCoordinator::GetPreview() const
{
    const double RawTime = Clock();
    const auto* Session = Client.GetSession();
    return Preview.IsSet() && Session && !Client.GetPending() && Preview->Session == *Session
        && FMath::IsFinite(RawTime) && FMath::Max(Now,RawTime) < PreviewDeadline ? &Preview.GetValue() : nullptr;
}
bool FRPGTradeCoordinator::RefreshPreview()
{
    if (!SyncContext() || bBusy || !Transport->CanSend() || Client.GetPending() || !Client.GetSession()) return false;
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
    if (!SyncContext() || bBusy || !Client.PrepareInvite(Invitee)) return false;
    InvalidatePreview();
    PendingAction = TEXT("invite"); Retries = 0; NextAttempt = Now;
    Dispatch(); return true;
}
bool FRPGTradeCoordinator::Submit(ERPGTradeAction Action, const FRPGTradeOffer* Offer)
{
    if (!SyncContext() || bBusy || !Client.Prepare(Action, Offer)) return false;
    InvalidatePreview();
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
    Dispatch(); return true;
}
bool FRPGTradeCoordinator::Dispatch()
{
    UpdateTime();
    if (!SyncContext() || bBusy || !Transport->CanSend() || Now < NextAttempt || !Client.GetPending()) return false;
    FRPGTradeTicket Ticket;
    if (!Client.BeginAttempt(Ticket)) return false;
    const auto Request = *Client.GetPending();
    const auto Expected = ++Operation;
    bBusy = true;
    TWeakPtr<FRPGTradeCoordinator> WeakThis = AsShared();
    auto Completion = [WeakThis, Ticket, Expected](FRPGTradeHttpResult Result)
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
        else { Self->PendingAction.Reset(); Self->Retries = 0; }
        Self->Notify();
    };
    const bool bSent = Transport->Send(Character, Request.TradeId, PendingAction, Request.Body, Completion);
    if (!bSent && Operation == Expected && bBusy) Completion(FRPGTradeHttpResult{});
    return bSent;
}
bool FRPGTradeCoordinator::Retry()
{
    UpdateTime();
    if (!SyncContext() || bBusy || !Client.GetPending() || Now < NextAttempt) return false;
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
    UpdateTime();
    if (SyncContext() && bAllowDispatch && Retries < 6) Dispatch();
}
