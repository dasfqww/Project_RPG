#include "Riding/RPGRidingSession.h"

bool FRPGRidingSessionIdentity::IsValid() const
{
    return AuthorityContext.IsValid() && CharacterId.IsValid() && DungeonSessionId.IsValid() && ConnectionGeneration > 0;
}

bool FRPGRidingSessionIdentity::operator==(const FRPGRidingSessionIdentity& Other) const
{
    return AuthorityContext == Other.AuthorityContext && CharacterId == Other.CharacterId
        && DungeonSessionId == Other.DungeonSessionId && ConnectionGeneration == Other.ConnectionGeneration;
}

bool FRPGRidingSession::ObserveTime(double Now)
{
    // Keep subsecond cooldown arithmetic meaningful even for a broken clock provider.
    if (!FMath::IsFinite(Now) || Now < 0 || Now < LastNow || Now > 1.e12) return false;
    LastNow = Now;
    return true;
}

void FRPGRidingSession::ResetContext()
{
    Identity = {};
    Policy = {};
    bAdmissionConfirmed = false;
    Snapshot = {};
    SnapshotRequest.Invalidate();
    SnapshotAttempts = 0;
    SnapshotRetryAt = 0;
    DashReadyAt = EmoteReadyAt = CombatLockUntil = 0;
    Epoch.Invalidate();
    ActiveRequest = 0;
    LastApprovedAction = {};
    Ledger.Reset(Epoch);
    // Budget and monotonic observation belong to the connection, not this identity/Pawn.
}

bool FRPGRidingSession::SetIdentity(const FRPGRidingSessionIdentity& InIdentity, double Now)
{
    if (Status == ERPGRidingSessionStatus::Revoked || !InIdentity.IsValid() || !ObserveTime(Now)) return false;
    if (Identity == InIdentity) return true;
    ResetContext();
    Identity = InIdentity;
    Status = ERPGRidingSessionStatus::WaitingAdmission;
    DashReadyAt = Now + 10;
    EmoteReadyAt = Now + 2;
    return true;
}

void FRPGRidingSession::ClearIdentity()
{
    ResetContext();
    if (Status != ERPGRidingSessionStatus::Revoked) Status = ERPGRidingSessionStatus::WaitingIdentity;
}

void FRPGRidingSession::Revoke()
{
    ResetContext();
    Status = ERPGRidingSessionStatus::Revoked;
}

bool FRPGRidingSession::ConfirmAdmission(const FRPGRidingSessionIdentity& Expected, const FRPGRidingPolicyValues& InPolicy)
{
    if (Status == ERPGRidingSessionStatus::Revoked || !Identity.IsValid() || !(Identity == Expected)) return false;
    // No hot reload or second policy installation in an admitted context, even with an unchanged hash.
    if (bAdmissionConfirmed) return false;
    TArray<FText> Errors;
    if (!InPolicy.Validate(Errors)) return false;
    Policy = InPolicy;
    bAdmissionConfirmed = true;
    Status = ERPGRidingSessionStatus::LoadingSnapshot;
    return true;
}

FRPGRidingSnapshotTicket FRPGRidingSession::BeginSnapshot(double Now, bool bUserRequested)
{
    if (!ObserveTime(Now) || !bAdmissionConfirmed || Status == ERPGRidingSessionStatus::Ready
        || SnapshotRequest.IsValid() || Now < SnapshotRetryAt || (SnapshotAttempts >= 4 && !bUserRequested)) return {};
    SnapshotRequest = FGuid::NewGuid();
    SnapshotAttempts = FMath::Min(SnapshotAttempts + 1, 4);
    Status = ERPGRidingSessionStatus::LoadingSnapshot;
    return {Identity, SnapshotRequest};
}

bool FRPGRidingSession::MatchesPending(const FRPGRidingSnapshotTicket& Ticket) const
{
    return bAdmissionConfirmed && Status == ERPGRidingSessionStatus::LoadingSnapshot
        && Ticket.RequestId.IsValid() && Ticket.RequestId == SnapshotRequest && Ticket.Identity == Identity;
}

bool FRPGRidingSession::FailSnapshot(const FRPGRidingSnapshotTicket& Ticket, double Now)
{
    if (!MatchesPending(Ticket) || !ObserveTime(Now)) return false;
    SnapshotRequest.Invalidate();
    Status = ERPGRidingSessionStatus::RetryableFailure;
    const double Delay = SnapshotAttempts < 4 ? static_cast<double>(1 << (SnapshotAttempts - 1)) : 10.0;
    SnapshotRetryAt = Now + Delay;
    return true;
}

bool FRPGRidingSession::ApplySnapshot(const FRPGRidingSnapshotTicket& Ticket, const FRPGRidingAdmissionSnapshot& Incoming, double Now)
{
    if (!MatchesPending(Ticket) || !ObserveTime(Now)) return false;
    const auto Invalid = [this, &Ticket, Now]() { FailSnapshot(Ticket, Now); return false; };
    if (!(Incoming.Identity == Identity) || !Incoming.RosterId.IsValid() || Incoming.OwnershipRevision < 0
        || Incoming.ProtocolVersion != Policy.ProtocolVersion || Incoming.CatalogVersion != Policy.CatalogVersion
        || Incoming.GameplayHash != Policy.GameplayHash || Incoming.OwnedMountIds.Num() > RPGRiding::MaxMountDefinitions
        || Incoming.Favorites.Num() > RPGRiding::MaxFavorites) return Invalid();
    TSet<FString> Owned;
    for (const FString& Id : Incoming.OwnedMountIds)
    {
        if (!RPGRiding::IsValidMountId(Id) || Owned.Contains(Id)) return Invalid();
        Owned.Add(Id);
    }
    if (!Incoming.SelectedMountId.IsEmpty() && (!RPGRiding::IsValidMountId(Incoming.SelectedMountId)
        || !Owned.Contains(Incoming.SelectedMountId))) return Invalid();
    TSet<FString> Favorites;
    for (const FString& Id : Incoming.Favorites)
    {
        if (!RPGRiding::IsValidMountId(Id) || !Owned.Contains(Id) || Favorites.Contains(Id)) return Invalid();
        Favorites.Add(Id);
    }
    Snapshot = Incoming;
    SnapshotRequest.Invalidate();
    Status = ERPGRidingSessionStatus::Ready;
    return true;
}

bool FRPGRidingSession::CopySnapshot(FRPGRidingAdmissionSnapshot& Out) const
{
    Out = {};
    if (Status != ERPGRidingSessionStatus::Ready) return false;
    Out = Snapshot;
    return true;
}

bool FRPGRidingSession::CopyMovementProfile(FRPGRidingMovementProfile& Out) const
{
    Out = {};
    if (Status != ERPGRidingSessionStatus::Ready) return false;
    Out.Version = Policy.CatalogVersion;
    Out.Speed = Policy.RideSpeed; Out.Acceleration = Policy.Acceleration;
    Out.Braking = Policy.Braking; Out.YawRate = Policy.YawRate;
    return Out.IsValid();
}

FGuid FRPGRidingSession::BindPawn()
{
    if (!Identity.IsValid() || Status == ERPGRidingSessionStatus::Revoked) return {};
    Epoch = FGuid::NewGuid();
    Ledger.Reset(Epoch);
    ActiveRequest = 0;
    LastApprovedAction = {};
    return Epoch;
}

bool FRPGRidingSession::UnbindPawn(const FGuid& ExpectedEpoch)
{
    if (!Epoch.IsValid() || ExpectedEpoch != Epoch) return false;
    Epoch.Invalidate();
    Ledger.Reset(Epoch);
    ActiveRequest = 0;
    LastApprovedAction = {};
    return true;
}

FRPGRidingRequestStart FRPGRidingSession::BeginCommand(const FRPGRidingSetCommand& Command,
    const FRPGRidingRepState& Current, double Now)
{
    FRPGRidingRequestStart Start;
    if (!ObserveTime(Now) || !Budget.TryConsume(Now))
    {
        Start.Receipt.Result = ERPGRidingResultCode::RateLimited;
        return Start;
    }
    if (!Identity.IsValid() || !Epoch.IsValid() || Status == ERPGRidingSessionStatus::Revoked)
    {
        Start.Receipt.Result = ERPGRidingResultCode::AuthorityRevoked;
        return Start;
    }
    if (Current.Key.RiderEpoch != Epoch || !RPGRiding::IsCanonical(Current)) return Start;
    Start = Ledger.Begin(Command);
    if (Start.Disposition != ERPGRidingRequestDisposition::Reserved) return Start;
    ERPGRidingResultCode Failure = ERPGRidingResultCode::Applied;
    if (ActiveRequest != 0) Failure = ERPGRidingResultCode::Busy;
    else if (Command.ExpectedStateRevision != Current.Key.StateRevision) Failure = ERPGRidingResultCode::StaleState;
    else if (Command.bDesiredMounted && Status != ERPGRidingSessionStatus::Ready) Failure = ERPGRidingResultCode::NotReady;
    if (Failure != ERPGRidingResultCode::Applied)
    {
        Start.Receipt = {Failure, Current};
        Ledger.Complete(Start.Token, Start.Receipt);
        Start.Token = {};
        Start.Disposition = ERPGRidingRequestDisposition::Rejected;
        return Start;
    }
    ActiveRequest = Command.RequestSeq;
    return Start;
}

bool FRPGRidingSession::CompleteCommand(const FRPGRidingRequestToken& Token, const FRPGRidingReceipt& Receipt)
{
    if (Token.Sequence != ActiveRequest || !Ledger.Complete(Token, Receipt)) return false;
    ActiveRequest = 0;
    return true;
}

bool FRPGRidingSession::TryReserveApprovedAction(const FRPGRidingRepState& Approved, double Now)
{
    if (!ObserveTime(Now) || !Budget.TryConsume(Now)) return false;
    if (Status != ERPGRidingSessionStatus::Ready || !Epoch.IsValid() || Approved.Key.RiderEpoch != Epoch
        || !RPGRiding::IsCanonical(Approved) || Approved.Mode != ERPGRidingMode::Mounted
        || Approved.Action == ERPGRidingAction::None || Now < CombatLockUntil) return false;
    if (LastApprovedAction.RiderEpoch.IsValid() && RPGRiding::Compare(Approved.Key, LastApprovedAction) != ERPGRidingStateOrder::Newer) return false;
    double& ReadyAt = Approved.Action == ERPGRidingAction::Dash ? DashReadyAt : EmoteReadyAt;
    if (Now < ReadyAt) return false;
    ReadyAt = Now + (Approved.Action == ERPGRidingAction::Dash ? Policy.DashCooldown : Policy.EmoteCooldown);
    LastApprovedAction = Approved.Key;
    return true;
}

bool FRPGRidingSession::RecordApprovedHostileOutcome(double Now)
{
    if (!bAdmissionConfirmed || !ObserveTime(Now)) return false;
    CombatLockUntil = FMath::Max(CombatLockUntil, Now + Policy.CombatLock);
    return true;
}

FRPGRidingTimingView FRPGRidingSession::GetTiming(double Now) const
{
    if (!Identity.IsValid() || !FMath::IsFinite(Now) || Now < LastNow || Now > 1.e12) return {};
    return {true, FMath::Max(0.0, DashReadyAt - Now), FMath::Max(0.0, EmoteReadyAt - Now), FMath::Max(0.0, CombatLockUntil - Now)};
}
