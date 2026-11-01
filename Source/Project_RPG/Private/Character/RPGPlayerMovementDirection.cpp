#include "Character/RPGPlayer.h"

#include "Character/RPGAbilityMovementDirection.h"
#include "GameFramework/CharacterMovementComponent.h"

FVector ARPGPlayer::GetAbilityMovementDirection() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || (!HasAuthority() && !IsLocallyControlled())) { return FVector::ZeroVector; }
	// LastControlInputVector is local Pawn input, not the remote ServerMove contract.
	// Never accept a separate ability RPC's direction/warp destination as authority.
	return RPGAbilityMovement::ResolveDirection(GetLastMovementInputVector(), Movement->GetCurrentAcceleration(),
		HasAuthority() && !IsLocallyControlled());
}
