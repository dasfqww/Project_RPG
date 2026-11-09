#pragma once

#include "Component/RPGCharacterMovementComponent.h"
#include "Riding/RPGRidingMovementPrediction.h"
#include "Riding/RPGRidingDashMotionHost.h"
#include "RPGPlayerMovementComponent.generated.h"

/** Keeps a revoked Pawn alive for queued Iris RPCs while refusing new movement. */
UCLASS()
class PROJECT_RPG_API URPGPlayerMovementComponent : public URPGCharacterMovementComponent, public IRPGRidingDashMotionHost
{
	GENERATED_BODY()
public:
    URPGPlayerMovementComponent();
    virtual bool CanOpenRidingDashWindow() const override;
    virtual FRPGRidingDashWindow OpenRidingDashWindow(float Duration, float Distance) override;
    virtual FVector ConstrainInputAcceleration(const FVector& Input) const override;
	virtual void SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode = 0) override;
	virtual void PerformMovement(float DeltaTime) override;
	virtual void ServerMove_HandleMoveData(const FCharacterNetworkMoveDataContainer& MoveDataContainer) override;
	void StopRevokedMovement();
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual bool CanCrouchInCurrentState() const override;
    virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
    virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response) override;
    virtual bool ServerCheckClientError(float Stamp, float Delta, const FVector& Accel, const FVector& ClientLocation,
        const FVector& RelativeLocation, FMovementBaseInterfaceData* Base, FName Bone, uint8 Mode) override;
protected:
    virtual bool TryGetRidingProfile(FRPGRidingMovementProfile& Out) const override;
    virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep,
        FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingMovementTestAccess;
#endif
	bool IsRevokedOnServer() const;
    FRPGRidingMoveDataContainer RidingMoves;
    FRPGRidingMoveResponse RidingResponse;
    FRPGRidingMoveStamp EvaluatedStamp;
    bool bEvaluatingRidingMove = false, bHasRidingStamp = false;
};
