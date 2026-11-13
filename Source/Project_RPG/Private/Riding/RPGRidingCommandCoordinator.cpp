#include "Riding/RPGRidingCommandCoordinator.h"
#include "HAL/PlatformTime.h"

FRPGRidingCommandCoordinator::FRPGRidingCommandCoordinator(TSharedRef<IRPGRidingOwnerTransport> T, FContext C, FString R)
    : Transport(MoveTemp(T)), Context(MoveTemp(C)), Root(MoveTemp(R)) {}
FRPGRidingCommandCoordinator::~FRPGRidingCommandCoordinator() { Shutdown(); }
void FRPGRidingCommandCoordinator::Shutdown()
{ bEnding = true; Active.Invalidate(); Transport->Cancel(); Pending.Reset(); Receipt.Reset(); Bound = {}; bEligible = false; bNeedsRefresh = false; State = ERPGRidingCommandState::Unavailable; }
void FRPGRidingCommandCoordinator::RefreshContext()
{
    if (bEnding || !IsInGameThread()) return;
    const auto C = Context ? Context() : FRPGRidingCommandContext{}; FString Endpoint, Account; FRPGRidingIntentScope Actual;
    const bool bCurrent = C.bEligible && C.Scope.IsValid() && Transport->HasCredential() && Transport->GetContext(Endpoint, Account)
        && FRPGRidingIntentScope::Create(Endpoint, Account, C.Scope.Character, Actual) && Actual == C.Scope;
    if (!(C.Scope == Bound))
    {
        Active.Invalidate(); Transport->Cancel(); Pending.Reset(); Receipt.Reset(); bNeedsRefresh = false; RetryAt = 0;
        Bound = C.Scope; bEligible = bCurrent; bUncertain = true;
        State = ERPGRidingCommandState::Unavailable;
        if (Bound.IsValid())
        {
            FRPGRidingPendingIntent I; const auto Read = FRPGRidingIntentJournal::Load(Root, Bound, I);
            if (Read == ERPGRidingJournalRead::Ready) { Pending = I; State = ERPGRidingCommandState::RecoveryRequired; }
            else State = Read == ERPGRidingJournalRead::Missing ? ERPGRidingCommandState::Idle : ERPGRidingCommandState::StorageBlocked;
        }
        OnChanged.Broadcast(); return;
    }
    if (bEligible == bCurrent) return;
    bEligible = bCurrent;
    if (!bCurrent) { Active.Invalidate(); Transport->Cancel(); if (Pending.IsSet()) { bUncertain = true; State = ERPGRidingCommandState::RecoveryRequired; } }
    OnChanged.Broadcast();
}
bool FRPGRidingCommandCoordinator::Submit(const FRPGRidingPendingIntent& I)
{
    RefreshContext(); if (bEnding || !bEligible || Active.IsValid() || Pending.IsSet() || bNeedsRefresh || State == ERPGRidingCommandState::StorageBlocked || !I.IsValid() || !(I.Scope == Bound)) return false;
    if (!FRPGRidingIntentJournal::Create(Root, I)) { State = ERPGRidingCommandState::StorageBlocked; OnChanged.Broadcast(); return false; }
    Pending = I; Receipt.Reset(); bUncertain = false; State = ERPGRidingCommandState::RecoveryRequired;
    return Dispatch(false);
}
bool FRPGRidingCommandCoordinator::Retry() { RefreshContext(); return Dispatch(false); }
bool FRPGRidingCommandCoordinator::Recover() { RefreshContext(); return Dispatch(true); }
bool FRPGRidingCommandCoordinator::Dispatch(bool bRecovery)
{
    if (bEnding || !IsInGameThread() || !bEligible || !Pending.IsSet() || Active.IsValid() || State == ERPGRidingCommandState::StorageBlocked
        || FPlatformTime::Seconds() < RetryAt || !Transport->CanSend()) return false;
    const auto I = Pending.GetValue(); const auto Nonce = Active = FGuid::NewGuid(); State = ERPGRidingCommandState::AwaitingServer;
    const TWeakPtr<FRPGRidingCommandCoordinator> Weak = AsShared(); auto Done = [Weak, Nonce, bRecovery](FRPGRidingOwnerHttpResult R)
    { if (const auto Self = Weak.Pin()) Self->Complete(Nonce, bRecovery, MoveTemp(R)); };
    const bool bSent = bRecovery ? Transport->ReadReceipt(I, MoveTemp(Done)) : Transport->SendIntent(I, MoveTemp(Done));
    if (!bSent && Active == Nonce) { Active.Invalidate(); bUncertain = true; State = ERPGRidingCommandState::RecoveryRequired; }
    OnChanged.Broadcast(); return bSent;
}
void FRPGRidingCommandCoordinator::Complete(FGuid Nonce, bool bRecovery, FRPGRidingOwnerHttpResult R)
{
    if (bEnding || Nonce != Active || !Active.IsValid() || !Pending.IsSet()) return;
    RefreshContext(); if (Nonce != Active || !bEligible || !Pending.IsSet()) return; Active.Invalidate();
    FRPGRidingReceipt Proof;
    if (R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode == 200 && RPGRidingReceipt::Decode(Pending.GetValue(), R.Body, bRecovery, Proof))
    { Receipt = Proof; Resolve(true); }
    else if (R.bCurrentPlayerContext && R.bTransportSuccessful && !bRecovery && !bUncertain && RPGRidingReceipt::DefinitiveFirstRejection(R.StatusCode, R.Body)) Resolve(false);
    else
    {
        bUncertain = true; State = ERPGRidingCommandState::RecoveryRequired;
        if (R.bCurrentPlayerContext && (R.StatusCode == 429 || R.StatusCode == 503))
            RetryAt = FPlatformTime::Seconds() + (FMath::IsFinite(R.RetryAfterSeconds) ? FMath::Clamp(R.RetryAfterSeconds, 1., 3600.) : 60.);
    }
    OnChanged.Broadcast();
}
void FRPGRidingCommandCoordinator::Resolve(bool bCommitted)
{
    bResolvedCommit = bCommitted;
    if (!Pending.IsSet() || !FRPGRidingIntentJournal::Clear(Root, Pending.GetValue())) { State = ERPGRidingCommandState::StorageBlocked; return; }
    Pending.Reset(); bNeedsRefresh = bCommitted; State = bCommitted ? ERPGRidingCommandState::Committed : ERPGRidingCommandState::Rejected;
}
bool FRPGRidingCommandCoordinator::RetryStorage()
{
    RefreshContext(); if (!bEligible || State != ERPGRidingCommandState::StorageBlocked || Active.IsValid()) return false;
    if (Pending.IsSet()) { Resolve(bResolvedCommit); OnChanged.Broadcast(); return State != ERPGRidingCommandState::StorageBlocked; }
    FRPGRidingPendingIntent I; const auto Read = FRPGRidingIntentJournal::Load(Root, Bound, I);
    if (Read == ERPGRidingJournalRead::Ready) { Pending = I; bUncertain = true; State = ERPGRidingCommandState::RecoveryRequired; }
    else if (Read == ERPGRidingJournalRead::Missing) State = ERPGRidingCommandState::Idle;
    OnChanged.Broadcast(); return State != ERPGRidingCommandState::StorageBlocked;
}
bool FRPGRidingCommandCoordinator::AcknowledgeFreshReads(FGuid Request)
{
    RefreshContext(); if (!bEligible || Pending.IsSet() || !Receipt.IsSet() || Request != Receipt->RequestId || !bNeedsRefresh) return false;
    bNeedsRefresh = false; OnChanged.Broadcast(); return true;
}
