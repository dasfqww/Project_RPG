#pragma once
#include "Riding/RPGRidingCollectionReader.h"
#include "Riding/RPGRidingRegistrationOptions.h"

/** Explicit owner-scoped metadata reads. No polling, mutations, durable intents or inventory authority. */
class PROJECT_RPG_API FRPGRidingRegistrationReader final : public TSharedFromThis<FRPGRidingRegistrationReader>
{
public:
    using FContext = FRPGRidingCollectionReader::FContext;
    FRPGRidingRegistrationReader(TSharedRef<IRPGRidingOwnerTransport> InTransport, FContext InContext);
    ~FRPGRidingRegistrationReader();
    void RefreshContext();
    bool Read();
    void Invalidate(); // Invalidates observations, retaining any current identity's Retry-After.
    void Shutdown();
    ERPGRidingCollectionReadState GetState() { RefreshContext(); return State; }
    const FRPGRidingRegistrationOptions* GetOptions() { RefreshContext(); return State == ERPGRidingCollectionReadState::Ready ? &Options : nullptr; }
    double GetRetryAfterSeconds() const;
    FSimpleMulticastDelegate OnChanged;
private:
    void Complete(FGuid Request, FRPGRidingCollectionContext Captured, FRPGRidingOwnerHttpResult Response);
    TSharedRef<IRPGRidingOwnerTransport> Transport;
    FContext Context;
    FRPGRidingCollectionContext Bound;
    FRPGRidingRegistrationOptions Options;
    ERPGRidingCollectionReadState State = ERPGRidingCollectionReadState::Unavailable;
    FGuid Pending;
    double RetryAt = 0;
    bool bEnding = false;
};
