#pragma once
#include "Riding/RPGRidingIntentJournal.h"
#include "Riding/RPGRidingOwnerTransport.h"
#include "Riding/RPGRidingReceipt.h"

struct FRPGRidingCommandContext { FRPGRidingIntentScope Scope; bool bEligible = false; };
enum class ERPGRidingCommandState : uint8 { Unavailable, Idle, AwaitingServer, RecoveryRequired, StorageBlocked, Committed, Rejected };
/** Persist-before-send, explicit recovery/retry, no inventory delta application or automatic dispatch. */
class PROJECT_RPG_API FRPGRidingCommandCoordinator final : public TSharedFromThis<FRPGRidingCommandCoordinator>
{
public:
    using FContext = TFunction<FRPGRidingCommandContext()>;
    FRPGRidingCommandCoordinator(TSharedRef<IRPGRidingOwnerTransport> Transport, FContext Context, FString JournalRoot);
    ~FRPGRidingCommandCoordinator();
    void RefreshContext();
    void Shutdown();
    bool Submit(const FRPGRidingPendingIntent& Intent);
    bool Retry();
    bool Recover();
    bool RetryStorage();
    bool AcknowledgeFreshReads(FGuid Request); // Owner calls only after successful current collection/inventory refresh.
    ERPGRidingCommandState GetState() { RefreshContext(); return bEligible ? State : ERPGRidingCommandState::Unavailable; }
    bool BlocksTransitions() { RefreshContext(); return Pending.IsSet() || bNeedsRefresh || State == ERPGRidingCommandState::StorageBlocked; }
    bool NeedsFreshReads() const { return bNeedsRefresh; }
    const FRPGRidingPendingIntent* GetPending() { RefreshContext(); return bEligible && Pending.IsSet() ? &Pending.GetValue() : nullptr; }
    const FRPGRidingReceipt* GetReceipt() { RefreshContext(); return bEligible && Receipt.IsSet() ? &Receipt.GetValue() : nullptr; }
    FSimpleMulticastDelegate OnChanged;
private:
    bool Dispatch(bool bRecovery);
    void Complete(FGuid Nonce, bool bRecovery, FRPGRidingOwnerHttpResult Response);
    void Resolve(bool bCommitted);
    TSharedRef<IRPGRidingOwnerTransport> Transport;
    FContext Context;
    FString Root;
    FRPGRidingIntentScope Bound;
    TOptional<FRPGRidingPendingIntent> Pending;
    TOptional<FRPGRidingReceipt> Receipt;
    ERPGRidingCommandState State = ERPGRidingCommandState::Unavailable;
    FGuid Active;
    double RetryAt = 0;
    bool bEligible = false, bUncertain = true, bNeedsRefresh = false, bEnding = false, bResolvedCommit = false;
};
