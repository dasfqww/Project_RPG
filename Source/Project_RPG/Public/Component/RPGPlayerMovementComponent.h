#pragma once

#include "Component/RPGCharacterMovementComponent.h"
#include "RPGPlayerMovementComponent.generated.h"

/** Keeps a revoked Pawn alive for queued Iris RPCs while refusing new movement. */
UCLASS()
class PROJECT_RPG_API URPGPlayerMovementComponent : public URPGCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode = 0) override;
	virtual void PerformMovement(float DeltaTime) override;
	virtual void ServerMove_HandleMoveData(const FCharacterNetworkMoveDataContainer& MoveDataContainer) override;
	void StopRevokedMovement();
private:
	bool IsRevokedOnServer() const;
};
