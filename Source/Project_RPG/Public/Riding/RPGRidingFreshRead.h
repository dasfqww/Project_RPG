#pragma once
#include "Riding/RPGRidingCommandCoordinator.h"
#include "Riding/RPGRidingCollection.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"

enum class ERPGRidingFreshReadState : uint8 { Idle, Unavailable, ReadingCollection, ReadingInventory, Failed, Ready };
/** Current reads after a correlated receipt. No receipt delta, write retry or automatic polling. */
class PROJECT_RPG_API FRPGRidingFreshRead final : public TSharedFromThis<FRPGRidingFreshRead>
{
public:
    struct FSink
    {
        TFunction<bool(FGuid)> Begin;
        TFunction<bool()> IsCurrent;
        TFunction<bool(const TArray<FRPGInventoryProjectionEntry>&, const TArray<FRPGInventoryProjectionEntry>&)> Inventory;
        TFunction<bool(const FRPGRidingCollection&)> Collection;
    };
    FRPGRidingFreshRead(TSharedRef<FRPGRidingCommandCoordinator> Commands, TSharedRef<IRPGRidingOwnerTransport> Transport,
        FRPGRidingCommandCoordinator::FContext Context, FSink Sink);
    ~FRPGRidingFreshRead();
    void RefreshContext();
    void Shutdown();
    bool Refresh();
    bool IsUnattempted() { RefreshContext(); return State == ERPGRidingFreshReadState::Idle && !bAttempted; }
    ERPGRidingFreshReadState GetState() { RefreshContext(); return State; }
    double GetRetryAfterSeconds() const;
private:
    bool Current(bool bRequireRefresh) const;
    void CompleteCollection(FGuid Nonce, FRPGRidingOwnerHttpResult Result);
    void CompleteInventory(FGuid Nonce, FRPGRidingOwnerHttpResult Result);
    void Fail(const FRPGRidingOwnerHttpResult& Result);
    TSharedRef<FRPGRidingCommandCoordinator> Commands;
    TSharedRef<IRPGRidingOwnerTransport> Transport;
    FRPGRidingCommandCoordinator::FContext Context;
    FSink Sink;
    FRPGRidingIntentScope Bound;
    FRPGRidingReceipt Proof;
    TOptional<FRPGRidingCollection> Incoming;
    FGuid Active;
    ERPGRidingFreshReadState State = ERPGRidingFreshReadState::Idle;
    double RetryAt = 0;
    bool bAttempted = false, bEnding = false;
};
