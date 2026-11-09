#pragma once

#include "Riding/RPGRidingPolicy.h"
#include "Riding/RPGRidingDashAdmission.h"
#include "Riding/RPGRidingRequestLedger.h"
#include "Security/RPGServerRequestBudget.h"

/** Native identity captured by the future authoritative actor adapter, never deserialized from an RPC.
 * AuthorityContext must change with PlayerState/controller/world/authentication context. */
struct FRPGRidingSessionIdentity
{
    FGuid AuthorityContext;
    FGuid CharacterId;
    FGuid DungeonSessionId;
    int64 ConnectionGeneration = 0;
    bool IsValid() const;
    bool operator==(const FRPGRidingSessionIdentity& Other) const;
};

enum class ERPGRidingSessionStatus : uint8
{
    WaitingIdentity, WaitingAdmission, LoadingSnapshot, Ready, RetryableFailure, Revoked
};

struct FRPGRidingAdmissionSnapshot
{
    FRPGRidingSessionIdentity Identity;
    FGuid RosterId;
    int64 OwnershipRevision = 0;
    int32 ProtocolVersion = 0;
    int32 CatalogVersion = 0;
    FString GameplayHash;
    TArray<FString> OwnedMountIds;
    FString SelectedMountId;
    TArray<FString> Favorites;
};

struct FRPGRidingSnapshotTicket
{
    FRPGRidingSessionIdentity Identity;
    FGuid RequestId;
};

struct FRPGRidingTimingView
{
    bool bValid = false;
    double DashRemaining = 0;
    double EmoteRemaining = 0;
    double CombatRemaining = 0;
};

/** Server-private session logic. No UObject/RPC/BP setters, authentication, HTTP or Pawn mutation.
 * The actor adapter must verify actual authority/current binding/admission before these native calls.
 * Keep one instance for the connection; replacing a Pawn must only BindPawn/UnbindPawn.
 * All time inputs are a server monotonic clock, all calls are on the game thread. */
class FRPGRidingSession
{
public:
    FRPGRidingSession() = default;
    FRPGRidingSession(const FRPGRidingSession&) = delete;
    FRPGRidingSession& operator=(const FRPGRidingSession&) = delete;
    bool SetIdentity(const FRPGRidingSessionIdentity& InIdentity, double Now);
    void ClearIdentity();
    void Revoke();
    bool ConfirmAdmission(const FRPGRidingSessionIdentity& Expected, const FRPGRidingPolicyValues& InPolicy);
    FRPGRidingSnapshotTicket BeginSnapshot(double Now, bool bUserRequested = false);
    bool ApplySnapshot(const FRPGRidingSnapshotTicket& Ticket, const FRPGRidingAdmissionSnapshot& Snapshot, double Now);
    bool FailSnapshot(const FRPGRidingSnapshotTicket& Ticket, double Now);
    ERPGRidingSessionStatus GetStatus() const { return Status; }
    bool CopySnapshot(FRPGRidingAdmissionSnapshot& Out) const;
    bool CopyMovementProfile(FRPGRidingMovementProfile& Out) const;
    bool CopyDashProfile(FRPGRidingDashProfile& Out) const;

    FGuid BindPawn();
    bool UnbindPawn(const FGuid& ExpectedEpoch);
    FRPGRidingRequestStart BeginCommand(const FRPGRidingSetCommand& Command, const FRPGRidingRepState& Current, double Now);
    bool CompleteCommand(const FRPGRidingRequestToken& Token, const FRPGRidingReceipt& Receipt);

    /** Caller has approved the execution against the current Pawn/GAS state. Reserve before callbacks.
     * Cancel/end/unbind do not refund. This is not the M2 activation/authorization implementation. */
    bool TryReserveApprovedAction(const FRPGRidingRepState& Approved, double Now);
    bool RecordApprovedHostileOutcome(double Now);
    FRPGRidingTimingView GetTiming(double Now) const;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingDashTestAccess;
#endif
    bool ObserveTime(double Now);
    bool MatchesPending(const FRPGRidingSnapshotTicket& Ticket) const;
    void ResetContext();
    FRPGRidingSessionIdentity Identity;
    ERPGRidingSessionStatus Status = ERPGRidingSessionStatus::WaitingIdentity;
    FRPGRidingPolicyValues Policy;
    bool bAdmissionConfirmed = false;
    FRPGRidingAdmissionSnapshot Snapshot;
    FGuid SnapshotRequest;
    int32 SnapshotAttempts = 0;
    double SnapshotRetryAt = 0;
    double LastNow = -1;
    double DashReadyAt = 0;
    double EmoteReadyAt = 0;
    double CombatLockUntil = 0;
    FGuid Epoch;
    uint32 ActiveRequest = 0;
    FRPGRidingStateKey LastApprovedAction;
    FRPGRidingRequestLedger Ledger;
    FRPGServerRequestBudget Budget{8, 4.0};
};
