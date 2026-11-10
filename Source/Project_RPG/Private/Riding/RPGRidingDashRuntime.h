#pragma once
#include "Riding/RPGRidingDashAdmission.h"
#include "Riding/RPGRidingDashGrant.h"
#include "Riding/RPGRidingDashMotionHost.h"

class URPGRidingComponent;
class ACharacter;
class UCharacterMovementComponent;

/** Server-only actual RMS execution. Admission, physics, resources and callbacks have one original owner. */
class FRPGRidingDashRuntime final
{
public:
    explicit FRPGRidingDashRuntime(URPGRidingComponent& InOwner) : Owner(InOwner) {}
    ERPGRidingResultCode Start(FRPGRidingStateKey Expected, FVector Direction, FRPGRidingDashActivation Activation = {});
    void PrepareMovement();
    void Tick();
    bool HasMotion() const;
    double Constrain(const FVector& Start, const FVector& End);
    void RecordStep(const FVector& Start, const FVector& End);
    void Stop(ERPGRidingEndReason Reason, bool Finish = true);
    void ClampAfterStateChange();
private:
    bool CanStart(const FRPGRidingDashProfile& Profile, const FVector& Direction) const;
    double SafeFraction(ACharacter& Character, UCharacterMovementComponent& Movement,
        const FVector& Start, const FVector& End, double Limit) const;
    struct FExecution
    {
        FRPGRidingDashTicket Ticket;
        TWeakObjectPtr<UCharacterMovementComponent> Movement;
        FRPGRidingDashWindow Window;
        FName MotionName;
        FVector LastPosition = FVector::ZeroVector;
        double Travelled = 0;
    } Active;
    URPGRidingComponent& Owner;
    TWeakObjectPtr<UCharacterMovementComponent> StoppedMovement;
    bool bStarting = false, bStopping = false, bSuppressRemainder = false, bStopAtBoundary = false;
};
