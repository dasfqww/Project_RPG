#include "Component/RPGPlayerMovementComponent.h"

#include "GameFramework/Character.h"
#include "Security/RPGDungeonGameplayAuthority.h"

bool URPGPlayerMovementComponent::IsRevokedOnServer() const
{
	return CharacterOwner && CharacterOwner->HasAuthority() && IsDungeonGameplayRevoked(CharacterOwner);
}

void URPGPlayerMovementComponent::StopRevokedMovement()
{
	if (!IsRevokedOnServer()) return;
	StopMovementImmediately();
	ClearAccumulatedForces();
	CurrentRootMotion.Clear();
	RootMotionParams.Clear();
	CharacterOwner->ConsumeMovementInputVector();
	Super::SetMovementMode(MOVE_None);
}

void URPGPlayerMovementComponent::SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode)
{
	Super::SetMovementMode(IsRevokedOnServer() ? MOVE_None : NewMovementMode,
		IsRevokedOnServer() ? 0 : NewCustomMode);
}

void URPGPlayerMovementComponent::PerformMovement(float DeltaTime)
{
	if (IsRevokedOnServer()) { StopRevokedMovement(); return; }
	Super::PerformMovement(DeltaTime);
}

void URPGPlayerMovementComponent::ServerMove_HandleMoveData(const FCharacterNetworkMoveDataContainer& MoveDataContainer)
{
	if (IsRevokedOnServer()) { StopRevokedMovement(); return; }
	Super::ServerMove_HandleMoveData(MoveDataContainer);
}
