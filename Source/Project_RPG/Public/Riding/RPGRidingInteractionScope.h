#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class APawn;
class AActor;

/** Synchronous server interaction boundary. The caller supplies its existing, read-only
 * target/distance/identity checks, including the target's pool generation or stable ID.
 * Checks run before dismount and again afterwards. Execute runs at most once, on foot,
 * with the original Pawn/possession/riding epoch. No callback or request is retained.
 * An accepted asynchronous operation owns its existing immutable observation/retry path. */
class PROJECT_RPG_API FRPGRidingInteractionScope final
{
public:
    static bool TryExecute(APawn& Pawn, AActor& Target,
        TFunctionRef<bool()> Validate, TFunctionRef<bool()> Execute);
    static bool HasPending(const APawn* Pawn);

    ~FRPGRidingInteractionScope();
    FRPGRidingInteractionScope(const FRPGRidingInteractionScope&) = delete;
    FRPGRidingInteractionScope& operator=(const FRPGRidingInteractionScope&) = delete;
private:
    explicit FRPGRidingInteractionScope(APawn& InPawn);
    TWeakObjectPtr<APawn> Pawn;
};
