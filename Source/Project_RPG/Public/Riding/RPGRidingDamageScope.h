#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UAbilitySystemComponent;
class URPGRidingSessionComponent;
class URPGRidingComponent;
class APawn;
struct FActiveGameplayEffectHandle;
enum class ERPGRidingEndReason : uint8;

/** Original-session barrier for final server damage/control application, before gameplay callbacks.
 * Complete with actual loss/absorption or the accepted control's active GE. No RPC/BP notification port. */
class PROJECT_RPG_API FRPGRidingDamageScope final
{
public:
    FRPGRidingDamageScope(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target);
    ~FRPGRidingDamageScope();
    FRPGRidingDamageScope(const FRPGRidingDamageScope&) = delete;
    FRPGRidingDamageScope& operator=(const FRPGRidingDamageScope&) = delete;
    void Complete(float HealthLoss, float ShieldAbsorbed);
    void CompleteControl(FActiveGameplayEffectHandle Handle);
private:
    void CommitOutcome(ERPGRidingEndReason Reason);
    struct FParticipant
    {
        TWeakObjectPtr<URPGRidingSessionComponent> Session;
        TWeakObjectPtr<URPGRidingComponent> Riding;
        FGuid Context, Token, RiderEpoch;
        bool bRecorded = false;
    };
    TArray<FParticipant, TInlineAllocator<2>> Participants;
    TWeakObjectPtr<UAbilitySystemComponent> ControlSource, ControlTarget;
    TWeakObjectPtr<APawn> ControlSourcePawn, ControlTargetPawn;
    bool bCompleted = false;
};
