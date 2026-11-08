#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UAbilitySystemComponent;
class URPGRidingSessionComponent;
class URPGRidingComponent;

/** Stack-local barrier at final server damage application, before shield/HP callbacks.
 * Complete only with actual health loss/shield absorption. No RPC/BP notification port. */
class PROJECT_RPG_API FRPGRidingDamageScope final
{
public:
    FRPGRidingDamageScope(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target);
    ~FRPGRidingDamageScope();
    FRPGRidingDamageScope(const FRPGRidingDamageScope&) = delete;
    FRPGRidingDamageScope& operator=(const FRPGRidingDamageScope&) = delete;
    void Complete(float HealthLoss, float ShieldAbsorbed);
private:
    struct FParticipant
    {
        TWeakObjectPtr<URPGRidingSessionComponent> Session;
        TWeakObjectPtr<URPGRidingComponent> Riding;
        FGuid Context, Token, RiderEpoch;
        bool bRecorded = false;
    };
    TArray<FParticipant, TInlineAllocator<2>> Participants;
    bool bCompleted = false;
};
