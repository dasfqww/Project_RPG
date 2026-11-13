#pragma once
#include "Riding/RPGRidingCollection.h"
#include "Riding/RPGRidingOwnerTransport.h"

struct FRPGRidingCollectionContext
{
    FString Endpoint, Account;
    FGuid Character;
    bool bEligible = false; // Current local authenticated lobby/selection, supplied by the owner.
    bool SameIdentity(const FRPGRidingCollectionContext& Other) const
    { return Endpoint == Other.Endpoint && Account == Other.Account && Character == Other.Character; }
};
enum class ERPGRidingCollectionReadState : uint8 { Unavailable, Empty, Loading, Ready, Failed, RateLimited };

/** Owner cache with explicit reads. Views must RefreshContext before displaying cached data. */
class PROJECT_RPG_API FRPGRidingCollectionReader final : public TSharedFromThis<FRPGRidingCollectionReader>
{
public:
    using FContext = TFunction<FRPGRidingCollectionContext()>;
    FRPGRidingCollectionReader(TSharedRef<IRPGRidingOwnerTransport> InTransport, FContext InContext);
    ~FRPGRidingCollectionReader();
    void RefreshContext();
    bool Read();
    void Shutdown();
    FRPGRidingCollectionContext GetContext() { RefreshContext(); return Bound; }
    ERPGRidingCollectionReadState GetState() { RefreshContext(); return State; }
    const FRPGRidingCollection* GetCollection() { RefreshContext(); return State == ERPGRidingCollectionReadState::Ready ? &Collection : nullptr; }
    double GetRetryAfterSeconds() const;
    FSimpleMulticastDelegate OnChanged;
private:
    void Complete(FGuid Request, FRPGRidingCollectionContext Captured, FRPGRidingOwnerHttpResult Response);
    TSharedRef<IRPGRidingOwnerTransport> Transport;
    FContext Context;
    FRPGRidingCollectionContext Bound;
    FRPGRidingCollection Collection;
    ERPGRidingCollectionReadState State = ERPGRidingCollectionReadState::Unavailable;
    FGuid Pending;
    double RetryAt = 0;
    bool bEnding = false;
};
