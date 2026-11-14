#include "Riding/RPGRidingFreshRead.h"
#include "Riding/RPGRidingIntentJson.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "HAL/PlatformTime.h"

FRPGRidingFreshRead::FRPGRidingFreshRead(TSharedRef<FRPGRidingCommandCoordinator> C, TSharedRef<IRPGRidingOwnerTransport> T,
    FRPGRidingCommandCoordinator::FContext F, FSink S) : Commands(MoveTemp(C)), Transport(MoveTemp(T)), Context(MoveTemp(F)), Sink(MoveTemp(S)) {}
FRPGRidingFreshRead::~FRPGRidingFreshRead() { Shutdown(); }
void FRPGRidingFreshRead::Shutdown()
{ bEnding = true; Active.Invalidate(); Transport->Cancel(); Incoming.Reset(); State = ERPGRidingFreshReadState::Unavailable; }
double FRPGRidingFreshRead::GetRetryAfterSeconds() const { return FMath::Max(0., RetryAt - FPlatformTime::Seconds()); }
bool FRPGRidingFreshRead::Current(bool bRequireRefresh) const
{
    if (bEnding || !IsInGameThread()) return false;
    const auto C = Context ? Context() : FRPGRidingCommandContext{}; FString E, A; FRPGRidingIntentScope Actual;
    const auto* R = Commands->GetReceipt();
    return C.bEligible && C.Scope == Bound && Transport->HasCredential() && Transport->GetContext(E, A)
        && FRPGRidingIntentScope::Create(E, A, Bound.Character, Actual) && Actual == Bound
        && R && R->RequestId == Proof.RequestId && R->Character == Bound.Character && R->Roster == Proof.Roster
        && !Commands->GetPending() && (!bRequireRefresh || Commands->NeedsFreshReads());
}
void FRPGRidingFreshRead::RefreshContext()
{
    if (bEnding || !IsInGameThread()) return;
    const auto C = Context ? Context() : FRPGRidingCommandContext{}; const auto* R = Commands->GetReceipt();
    if (!(C.Scope == Bound) || (R && R->RequestId != Proof.RequestId) || Commands->GetPending())
    {
        Active.Invalidate(); Transport->Cancel(); Incoming.Reset(); RetryAt = 0; bAttempted = false;
        Bound = C.Scope; Proof = R ? *R : FRPGRidingReceipt{}; State = ERPGRidingFreshReadState::Idle;
    }
    if (!Current(false))
    {
        if (Active.IsValid()) { Active.Invalidate(); Transport->Cancel(); Incoming.Reset(); }
        State = ERPGRidingFreshReadState::Unavailable; return;
    }
    if (State == ERPGRidingFreshReadState::Unavailable) State = bAttempted ? ERPGRidingFreshReadState::Failed : ERPGRidingFreshReadState::Idle;
    if (!Commands->NeedsFreshReads() && Active.IsValid()) { Active.Invalidate(); Transport->Cancel(); Incoming.Reset(); State = ERPGRidingFreshReadState::Idle; }
}
bool FRPGRidingFreshRead::Refresh()
{
    RefreshContext();
    if (!Current(true) || Active.IsValid() || GetRetryAfterSeconds() > 0 || !Transport->CanSend()
        || !Sink.Begin || !Sink.IsCurrent || !Sink.Inventory || !Sink.Collection) return false;
    const auto Nonce = Active = FGuid::NewGuid(); bAttempted = true; State = ERPGRidingFreshReadState::ReadingCollection; Incoming.Reset();
    const bool Began = Sink.Begin(Bound.Character);
    const bool SinkCurrent = Began && Sink.IsCurrent();
    if (Active != Nonce || !Current(true)) return false;
    if (!SinkCurrent) { Fail({}); return false; }
    const TWeakPtr<FRPGRidingFreshRead> Weak = AsShared();
    const bool Sent = Transport->ReadCollection(Bound.Character, [Weak, Nonce](FRPGRidingOwnerHttpResult R)
        { if (const auto Self = Weak.Pin()) Self->CompleteCollection(Nonce, MoveTemp(R)); });
    if (!Sent && Active == Nonce) Fail({});
    return Sent;
}
void FRPGRidingFreshRead::Fail(const FRPGRidingOwnerHttpResult& R)
{
    if (bEnding || !Active.IsValid()) return;
    Active.Invalidate(); Incoming.Reset(); State = ERPGRidingFreshReadState::Failed;
    if (R.bCurrentPlayerContext && (R.StatusCode == 429 || R.StatusCode == 503))
        RetryAt = FPlatformTime::Seconds() + (FMath::IsFinite(R.RetryAfterSeconds) ? FMath::Clamp(R.RetryAfterSeconds, 1., 3600.) : 60.);
}
void FRPGRidingFreshRead::CompleteCollection(FGuid Nonce, FRPGRidingOwnerHttpResult R)
{
    RefreshContext(); if (Active != Nonce || !Current(true)) return;
    FRPGRidingCollection C;
    const bool Valid = R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode == 200 && RPGRidingCollection::Decode(R.Body, Bound.Character, C)
        && C.RosterId == Proof.Roster && (Proof.Operation == ERPGRidingPersistentOperation::Register ? C.RosterRevision : C.PreferencesRevision) >= Proof.Revision
        && (Proof.Operation != ERPGRidingPersistentOperation::Register || C.Mounts.ContainsByPredicate([this](const FRPGRidingOwnedMount& M) { return M.MountId == Proof.MountId; }))
        && Sink.IsCurrent();
    if (Active != Nonce || !Current(true)) return;
    if (!Valid) { Fail(R); return; }
    Incoming = MoveTemp(C); State = ERPGRidingFreshReadState::ReadingInventory;
    const TWeakPtr<FRPGRidingFreshRead> Weak = AsShared();
    const bool Sent = Transport->ReadInventory(Bound.Character, [Weak, Nonce](FRPGRidingOwnerHttpResult Result)
        { if (const auto Self = Weak.Pin()) Self->CompleteInventory(Nonce, MoveTemp(Result)); });
    if (!Sent && Active == Nonce) Fail({});
}
void FRPGRidingFreshRead::CompleteInventory(FGuid Nonce, FRPGRidingOwnerHttpResult R)
{
    RefreshContext(); if (Active != Nonce || !Current(true) || !Incoming.IsSet()) return;
    TArray<FRPGItemRecord> Records; TArray<FRPGInventoryProjectionEntry> Items, Mail;
    const FRPGItemOwnerRef Owner { ERPGItemOwnerType::Character, Bound.Character.ToString(EGuidFormats::DigitsWithHyphensLower) };
    // This unpaginated endpoint cannot prove completeness at its 500-record limit.
    const bool Valid = R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode == 200 && R.Body.Len() <= 1024 * 1024
        && RPGRidingIntentJson::UniqueJson(R.Body) && FRPGItemBackendJsonCodec::DeserializeLoadResponse(R.Body, Records) && Records.Num() < 500
        && FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner, Records, Items)
        && FRPGInventoryProjectionMapper::BuildMailSnapshot(Owner, Records, Mail) && Sink.IsCurrent();
    if (Active != Nonce || !Current(true)) return;
    if (!Valid) { Fail(R); return; }
    const bool Applied = Sink.Inventory(Items, Mail);
    const bool InventoryCurrent = Applied && Sink.IsCurrent();
    if (Active != Nonce || !Current(true)) return;
    if (!InventoryCurrent) { Fail({}); return; }
    const auto C = Incoming.GetValue(); const bool Installed = Sink.Collection(C);
    const bool CollectionCurrent = Installed && Sink.IsCurrent();
    if (Active != Nonce || !Current(true)) return;
    if (!CollectionCurrent) { Fail({}); return; }
    const bool Acknowledged = Commands->AcknowledgeFreshReads(Proof.RequestId);
    if (Active != Nonce) return;
    Active.Invalidate(); Incoming.Reset();
    State = Acknowledged && Current(false) ? ERPGRidingFreshReadState::Ready : ERPGRidingFreshReadState::Failed;
}
