#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
class APawn;

/** Server-local original-Pawn reservation, released only by its owning token. */
class PROJECT_RPG_API FRPGRidingTimedInteractionLease final
{
public:
    static bool HasPending(const APawn* Pawn);
    ~FRPGRidingTimedInteractionLease();
    FRPGRidingTimedInteractionLease(const FRPGRidingTimedInteractionLease&) = delete;
    FRPGRidingTimedInteractionLease& operator=(const FRPGRidingTimedInteractionLease&) = delete;
private:
    friend class URPGRidingTimedInteractionComponent;
    static TSharedPtr<FRPGRidingTimedInteractionLease> TryAcquire(APawn& Pawn);
    FRPGRidingTimedInteractionLease(APawn& InPawn, FGuid InToken);
    TWeakObjectPtr<APawn> Pawn;
    FGuid Token;
};
