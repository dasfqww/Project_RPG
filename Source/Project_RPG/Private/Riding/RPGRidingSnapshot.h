#pragma once

#include "Riding/RPGRidingSession.h"
#include "HttpFwd.h"

class IRPGDungeonRecoveryHost;
class UWorld;
struct FRPGRidingSnapshotContext
{
    FRPGRidingSnapshotTicket Ticket;
    FRPGRidingPolicyValues Policy;
    FString ServerId;
    TWeakObjectPtr<UWorld> World;
    FDateTime SentUtc;
    double SentMonotonic = 0;
};
struct FRPGRidingSnapshotResponse
{
    bool bScopeCurrent = false;
    bool bTransportSuccessful = false;
    int32 StatusCode = 0;
    FString Body;
};
struct FRPGRidingDecodedSnapshot
{
    FRPGRidingAdmissionSnapshot Snapshot;
    double ValidForSeconds = 0;
};
namespace RPGRidingSnapshot
{
    constexpr int32 MaximumBytes = 256 * 1024;
    bool Decode(const FString& Body, const FRPGRidingSnapshotContext& Expected, FDateTime ReceivedUtc,
        double ReceivedMonotonic, FRPGRidingDecodedSnapshot& Out);
    FHttpRequestPtr Create(IRPGDungeonRecoveryHost& Host, const FRPGRidingSnapshotContext& Context,
        TFunction<void(const FRPGRidingSnapshotResponse&)> Completion);
    // Separate resolver keeps the wire/transport independent of the concrete manager and its reserved source files.
    IRPGDungeonRecoveryHost* ResolveHost(UWorld* World);
}
