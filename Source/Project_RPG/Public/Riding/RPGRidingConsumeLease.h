#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class APawn;

/** Server-local original-Pawn reservation. Retain with the prepared consume target until definitive discard/delivery.
 * Shared ownership deliberately keeps the barrier alive across reentrant gameplay callbacks. No RPC/BP setters. */
class PROJECT_RPG_API FRPGRidingConsumeLease final
{
public:
    static TSharedPtr<FRPGRidingConsumeLease> TryAcquire(APawn& Pawn);
    static bool HasPending(const APawn* Pawn);
    ~FRPGRidingConsumeLease();
    FRPGRidingConsumeLease(const FRPGRidingConsumeLease&) = delete;
    FRPGRidingConsumeLease& operator=(const FRPGRidingConsumeLease&) = delete;
private:
    FRPGRidingConsumeLease(APawn& InPawn, const FGuid& InToken);
    TWeakObjectPtr<APawn> Pawn;
    FGuid Token;
};
