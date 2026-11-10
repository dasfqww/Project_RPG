#pragma once
#include "Riding/RPGRidingDashGrant.h"
#include "Riding/RPGRidingDashIntent.h"
class URPGRidingComponent;
class UCharacterMovementComponent;
struct FRPGRidingMoveStamp;

/** Owner presentation/movement only. Never publishes State, a server window or a session cooldown. */
class FRPGRidingDashPrediction final
{
public:
    explicit FRPGRidingDashPrediction(URPGRidingComponent& InOwner) : Owner(InOwner) {}
    bool Start(FRPGRidingDashActivation Activation, const FRPGRidingDashIntent& Intent);
    void Stop();
    void Update();
    void Prepare();
    void Finalize();
    bool IsPending() const { return Activation.IsValid(); }
    bool HasMotion() const;
    FRPGRidingRepState View() const;
    float CooldownRemaining() const;
    double Constrain(const FVector& Start, const FVector& End);
    void Record(FGuid Execution, const FVector& Start, const FVector& End);
    double GetTravelled() const { return Travelled; }
    void Restore(const FRPGRidingMoveStamp& Stamp, double Distance);
private:
    bool ContextCurrent() const;
    bool ForeignMotion() const;
    void ClampVelocity();
    URPGRidingComponent& Owner;
    FRPGRidingDashActivation Activation;
    FRPGRidingStateKey Expected;
    FRPGRidingRepState Predicted;
    FRPGRidingStateKey RetiredKey;
    TWeakObjectPtr<UCharacterMovementComponent> Movement;
    FName MotionName;
    FVector LastPosition = FVector::ZeroVector;
    double StartedAt = 0, Travelled = 0;
    bool bAcknowledged = false, bSuppressRemainder = false, bBoundary = false;
};
