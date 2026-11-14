#include "Riding/RPGRidingCollectionReader.h"
#include "HAL/PlatformTime.h"

FRPGRidingCollectionReader::FRPGRidingCollectionReader(TSharedRef<IRPGRidingOwnerTransport> InTransport, FContext InContext)
    : Transport(MoveTemp(InTransport)), Context(MoveTemp(InContext)) {}
FRPGRidingCollectionReader::~FRPGRidingCollectionReader() { Shutdown(); }
void FRPGRidingCollectionReader::Shutdown()
{
    bEnding = true; Pending.Invalidate(); Transport->Cancel(); Collection = {}; Bound = {}; RetryAt = 0;
    State = ERPGRidingCollectionReadState::Unavailable;
}
void FRPGRidingCollectionReader::RefreshContext()
{
    if (bEnding || !IsInGameThread()) return;
    auto Current = Context ? Context() : FRPGRidingCollectionContext{}; FString Endpoint, Account;
    Current.bEligible &= Current.Character.IsValid() && Transport->HasCredential() && Transport->GetContext(Endpoint, Account)
        && Current.Endpoint == Endpoint && Current.Account == Account;
    if (Bound.SameIdentity(Current) && Bound.bEligible == Current.bEligible) return;
    Bound = Current; Pending.Invalidate(); Transport->Cancel(); Collection = {}; RetryAt = 0;
    State = Current.bEligible ? ERPGRidingCollectionReadState::Empty : ERPGRidingCollectionReadState::Unavailable;
    OnChanged.Broadcast();
}
bool FRPGRidingCollectionReader::Read()
{
    RefreshContext();
    if (bEnding || !Bound.bEligible || Pending.IsValid() || FPlatformTime::Seconds() < RetryAt || !Transport->CanSend()) return false;
    const FGuid Request = Pending = FGuid::NewGuid(); const auto Captured = Bound;
    Collection = {}; State = ERPGRidingCollectionReadState::Loading;
    const TWeakPtr<FRPGRidingCollectionReader> Weak = AsShared();
    const bool bSent = Transport->ReadCollection(Bound.Character, [Weak, Request, Captured](FRPGRidingOwnerHttpResult Result)
        { if (auto Self = Weak.Pin()) Self->Complete(Request, Captured, MoveTemp(Result)); });
    if (!bSent && Pending == Request) { Pending.Invalidate(); State = ERPGRidingCollectionReadState::Failed; }
    OnChanged.Broadcast(); return bSent;
}
void FRPGRidingCollectionReader::Complete(FGuid Request, FRPGRidingCollectionContext Captured, FRPGRidingOwnerHttpResult Response)
{
    if (bEnding || !IsInGameThread() || Request != Pending) return;
    RefreshContext(); if (Request != Pending || !Bound.SameIdentity(Captured) || !Bound.bEligible) return;
    Pending.Invalidate(); Collection = {};
    if (!Response.bCurrentPlayerContext) { Bound.bEligible = false; State = ERPGRidingCollectionReadState::Unavailable; }
    else if (Response.bTransportSuccessful && Response.StatusCode == 200 && RPGRidingCollection::Decode(Response.Body, Bound.Character, Collection))
        State = ERPGRidingCollectionReadState::Ready;
    else
    {
        const bool bLimited = Response.StatusCode == 429 || Response.StatusCode == 503;
        State = bLimited ? ERPGRidingCollectionReadState::RateLimited : ERPGRidingCollectionReadState::Failed;
        if (bLimited) RetryAt = FPlatformTime::Seconds() + (FMath::IsFinite(Response.RetryAfterSeconds) ? FMath::Clamp(Response.RetryAfterSeconds, 1., 3600.) : 60.);
    }
    OnChanged.Broadcast();
}
double FRPGRidingCollectionReader::GetRetryAfterSeconds() const { return FMath::Max(0., RetryAt - FPlatformTime::Seconds()); }
bool FRPGRidingCollectionReader::InstallFreshSnapshot(const FRPGRidingCollectionContext& C, const FRPGRidingCollection& Snapshot)
{
    RefreshContext();
    if (bEnding || !IsInGameThread() || !Bound.bEligible || !Bound.SameIdentity(C) || !C.bEligible || Pending.IsValid()
        || Snapshot.CharacterId != Bound.Character || !Snapshot.RosterId.IsValid()) return false;
    if (State == ERPGRidingCollectionReadState::Ready && (Collection.RosterId != Snapshot.RosterId
        || Collection.RosterRevision > Snapshot.RosterRevision || Collection.PreferencesRevision > Snapshot.PreferencesRevision)) return false;
    Collection = Snapshot; State = ERPGRidingCollectionReadState::Ready; RetryAt = 0;
    OnChanged.Broadcast();
    RefreshContext();
    return !bEnding && Bound.bEligible && Bound.SameIdentity(C) && State == ERPGRidingCollectionReadState::Ready
        && Collection.RosterId == Snapshot.RosterId && Collection.RosterRevision == Snapshot.RosterRevision
        && Collection.PreferencesRevision == Snapshot.PreferencesRevision && !Pending.IsValid();
}
