#include "Item/RPGSwordWaveProjectile.h"

#include "Ability/Skill/RPGSwordWaveAbility.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSwordWaveProjectile)

ARPGSwordWaveProjectile::ARPGSwordWaveProjectile()
{
	bReplicates = true;
	SetReplicateMovement(true);
	// A 0.3 second flight at 1800 cm/s covers up to 5.4 m before impact.
	InitialLifeSpan = 0.3f;
	ProjectileMovementComp->InitialSpeed = 1800.f;
	ProjectileMovementComp->MaxSpeed = 1800.f;
	ProjectileCollisionBox->InitBoxExtent(FVector(12.f, 45.f, 32.f));
}

void ARPGSwordWaveProjectile::BeginPlay()
{
	if (HasAuthority())
	{
		ProjectileCollisionBox->IgnoreActorWhenMoving(GetInstigator(), true);
	}
	else
	{
		SetActorEnableCollision(false);
		ProjectileMovementComp->Deactivate();
	}
	Super::BeginPlay();
}

bool ARPGSwordWaveProjectile::ApplyDamageFromImpact(APawn* Target, const FHitResult& ServerHit)
{
	URPGSwordWaveAbility* Cast = OwningCast.Get();
	return Cast && Cast->ApplySwordWaveHit(*this, Target, ServerHit);
}
