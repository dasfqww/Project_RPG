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
            // A read cannot shorten a server-directed retry wait or resolve an uncertain write.
            Self->Notify();
        });
    if (!bSent && Operation == Expected)
    { bBusy = false; LastResponse = Client.CompleteRead(Ticket, 0, TEXT("")); Notify(); }
    return bSent;
}
bool FRPGTradeCoordinator::Invite(FGuid Invitee)
{
    if (!SyncContext() || bBusy || !Client.PrepareInvite(Invitee)) return false;
    PendingAction = TEXT("invite"); Retries = 0; NextAttempt = Now;
    Dispatch(); return true;
}
bool FRPGTradeCoordinator::Submit(ERPGTradeAction Action, const FRPGTradeOffer* Offer)
{
    if (!SyncContext() || bBusy || !Client.Prepare(Action, Offer)) return false;
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
