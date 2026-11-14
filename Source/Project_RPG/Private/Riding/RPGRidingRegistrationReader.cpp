#include "Riding/RPGRidingRegistrationReader.h"
#include "HAL/PlatformTime.h"

FRPGRidingRegistrationReader::FRPGRidingRegistrationReader(TSharedRef<IRPGRidingOwnerTransport> InTransport, FContext InContext)
    : Transport(MoveTemp(InTransport)), Context(MoveTemp(InContext)) {}
FRPGRidingRegistrationReader::~FRPGRidingRegistrationReader() { Shutdown(); }
void FRPGRidingRegistrationReader::Shutdown()
{
    bEnding = true; Pending.Invalidate(); Transport->Cancel(); Options = {}; Bound = {}; RetryAt = 0;
    State = ERPGRidingCollectionReadState::Unavailable;
}
void FRPGRidingRegistrationReader::RefreshContext()
{
    if (bEnding || !IsInGameThread()) return;
    auto Current = Context ? Context() : FRPGRidingCollectionContext{}; FString Endpoint, Account;
    Current.bEligible &= Current.Character.IsValid() && Transport->HasCredential() && Transport->GetContext(Endpoint, Account)
        && Current.Endpoint == Endpoint && Current.Account == Account;
    if (Bound.SameIdentity(Current) && Bound.bEligible == Current.bEligible) return;
    Bound = Current; Pending.Invalidate(); Transport->Cancel(); Options = {}; RetryAt = 0;
    State = Bound.bEligible ? ERPGRidingCollectionReadState::Empty : ERPGRidingCollectionReadState::Unavailable;
    OnChanged.Broadcast();
}
void FRPGRidingRegistrationReader::Invalidate()
{
    RefreshContext(); if (bEnding || !IsInGameThread()) return;
    Pending.Invalidate(); Transport->Cancel(); Options = {};
    State = !Bound.bEligible ? ERPGRidingCollectionReadState::Unavailable
        : GetRetryAfterSeconds() > 0 ? ERPGRidingCollectionReadState::RateLimited : ERPGRidingCollectionReadState::Empty;
    OnChanged.Broadcast();
}
bool FRPGRidingRegistrationReader::Read()
{
    RefreshContext();
    if (bEnding || !IsInGameThread() || !Bound.bEligible || Pending.IsValid() || GetRetryAfterSeconds() > 0 || !Transport->CanSend()) return false;
    const FGuid Request = Pending = FGuid::NewGuid(); const auto Captured = Bound;
    Options = {}; State = ERPGRidingCollectionReadState::Loading;
    const TWeakPtr<FRPGRidingRegistrationReader> Weak = AsShared();
    const bool bSent = Transport->ReadRegistrationOptions(Bound.Character, [Weak, Request, Captured](FRPGRidingOwnerHttpResult Result)
        { if (auto Self = Weak.Pin()) Self->Complete(Request, Captured, MoveTemp(Result)); });
    if (!bSent && Pending == Request) { Pending.Invalidate(); State = ERPGRidingCollectionReadState::Failed; }
    OnChanged.Broadcast(); return bSent;
}
void FRPGRidingRegistrationReader::Complete(FGuid Request, FRPGRidingCollectionContext Captured, FRPGRidingOwnerHttpResult Response)
{
    if (bEnding || !IsInGameThread() || Request != Pending) return;
    RefreshContext(); if (Request != Pending || !Bound.SameIdentity(Captured) || !Bound.bEligible) return;
    Pending.Invalidate(); Options = {}; RetryAt = 0;
    if (!Response.bCurrentPlayerContext) { Bound.bEligible = false; State = ERPGRidingCollectionReadState::Unavailable; }
    else if (Response.bTransportSuccessful && Response.StatusCode == 200 && RPGRidingRegistrationOptions::Decode(Response.Body, Bound.Character, Options))
        State = ERPGRidingCollectionReadState::Ready;
    else
    {
        const bool bLimited = Response.bTransportSuccessful && (Response.StatusCode == 429 || Response.StatusCode == 503);
        State = bLimited ? ERPGRidingCollectionReadState::RateLimited : ERPGRidingCollectionReadState::Failed;
        if (bLimited) RetryAt = FPlatformTime::Seconds() + (FMath::IsFinite(Response.RetryAfterSeconds) ? FMath::Clamp(Response.RetryAfterSeconds, 1., 3600.) : 60.);
    }
    OnChanged.Broadcast();
}
double FRPGRidingRegistrationReader::GetRetryAfterSeconds() const { return FMath::Max(0., RetryAt - FPlatformTime::Seconds()); }
