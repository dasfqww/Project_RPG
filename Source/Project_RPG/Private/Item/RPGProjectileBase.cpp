// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/RPGProjectileBase.h"
#include "Combat/RPGCombatImmunityTypes.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Level.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "Manager/PoolManager.h"
#include "RPGGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "TimerManager.h"

namespace
{
	constexpr int32 MaxIdleProjectilesPerClass = 32;
}

// Sets default values
ARPGProjectileBase::ARPGProjectileBase()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	ProjectileCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ProjectileCollisionBox"));
	SetRootComponent(ProjectileCollisionBox);
	ProjectileCollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	ProjectileCollisionBox->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnProjectileHit);
	ProjectileCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnProjectileBeginOverlap);

	ProjectileNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileNiagaraComponent"));
	ProjectileNiagaraComponent->SetupAttachment(GetRootComponent());

	ProjectileMovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComp"));
	ProjectileMovementComp->InitialSpeed = 700.f;
	ProjectileMovementComp->MaxSpeed = 900.f;
	ProjectileMovementComp->Velocity = FVector(1.f, 0.f, 0.f);
	ProjectileMovementComp->ProjectileGravityScale = 0.f;

	InitialLifeSpan = 4.f;
}

ARPGProjectileBase* ARPGProjectileBase::LaunchPooledProjectile(TSubclassOf<ARPGProjectileBase> ProjectileClass,
	FVector Location, FRotator Rotation, APawn* ShootingPawn, FGameplayEffectSpecHandle DamageEffectSpec)
{
	UWorld* World = IsValid(ShootingPawn) ? ShootingPawn->GetWorld() : nullptr;
	const ARPGProjectileBase* Defaults = ProjectileClass.GetDefaultObject();
	if (!World || World->bIsTearingDown || !Defaults || Location.ContainsNaN() || Rotation.ContainsNaN())
	{
		return nullptr;
	}

	UPoolManager* Pools = UPoolManager::Get(World);
	FName PoolName;
	if (!Defaults->GetIsReplicated())
	{
		PoolName = FName(*FString::Printf(TEXT("Projectile:%s:%s"),
			*ShootingPawn->GetLevel()->GetPathName(), *ProjectileClass->GetPathName()));
		Pools->RegisterPool(PoolName, ProjectileClass, 0);
		if (ARPGProjectileBase* Projectile = Cast<ARPGProjectileBase>(Pools->AcquireInactiveFromPool(PoolName)))
		{
			Projectile->Relaunch(Location, Rotation, ShootingPawn, DamageEffectSpec);
			return IsValid(Projectile) ? Projectile : nullptr;
		}
	}

	const FTransform Transform(Rotation, Location);
	ARPGProjectileBase* Projectile = World->SpawnActorDeferred<ARPGProjectileBase>(
		ProjectileClass, Transform, ShootingPawn, ShootingPawn, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!IsValid(Projectile)) { return nullptr; }
	Projectile->ProjectileDamageEffectSpecHandle = DamageEffectSpec;
	Projectile->FinishSpawning(Transform);
	if (!IsValid(Projectile)) { return nullptr; }
	if (!PoolName.IsNone() && !Projectile->GetIsReplicated() && Pools->TrackActorForPool(PoolName, Projectile))
	{
		Projectile->OwningPoolName = PoolName;
	}
	return Projectile;
}

// Called when the game starts or when spawned
void ARPGProjectileBase::BeginPlay()
{
	if (ProjectileDamagePolicy == EProjectileDamagePolicy::OnBeginOverlap)
	{
		ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
	Super::BeginPlay();
	if (IsValid(this) && !bImpactHandled)
	{
		OnProjectileLaunched();
	}
}

void ARPGProjectileBase::Relaunch(const FVector& Location, const FRotator& Rotation, APawn* ShootingPawn,
	const FGameplayEffectSpecHandle& DamageEffectSpec)
{
	const ARPGProjectileBase* Defaults = GetClass()->GetDefaultObject<ARPGProjectileBase>();
	const UProjectileMovementComponent* DefaultMovement = Defaults->ProjectileMovementComp;
	SetOwner(ShootingPawn);
	SetInstigator(ShootingPawn);
	ProjectileDamageEffectSpecHandle = DamageEffectSpec;

	{
		FScopedMovementUpdate MovementScope(ProjectileCollisionBox, EScopedUpdate::DeferredUpdates);
		SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		ProjectileMovementComp->SetUpdatedComponent(ProjectileCollisionBox);
		SetActorEnableCollision(true);
		FVector AdjustedLocation = Location;
		if (GetWorld()->FindTeleportSpot(this, AdjustedLocation, Rotation))
		{
			SetActorLocation(AdjustedLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}

		FVector Velocity = DefaultMovement->Velocity;
		if (DefaultMovement->InitialSpeed > 0.f)
		{
			Velocity = Velocity.GetSafeNormal() * DefaultMovement->InitialSpeed;
		}
		ProjectileMovementComp->Velocity = DefaultMovement->bInitialVelocityInLocalSpace
			? Rotation.RotateVector(Velocity) : Velocity;
		if (ProjectileMovementComp->bRotationFollowsVelocity && !Velocity.IsNearlyZero())
		{
			FRotator FlightRotation = ProjectileMovementComp->Velocity.Rotation();
			if (ProjectileMovementComp->bRotationRemainsVertical)
			{
				FlightRotation.Pitch = 0.f;
				FlightRotation.Roll = 0.f;
			}
			SetActorRotation(FlightRotation, ETeleportType::TeleportPhysics);
		}
		ProjectileMovementComp->PreviousHitTime = 1.f;
		ProjectileMovementComp->PreviousHitNormal = FVector::ZeroVector;
		ProjectileMovementComp->bIsSliding = false;
		ProjectileMovementComp->HomingTargetComponent.Reset();
		ProjectileMovementComp->ClearPendingForce(true);
		ProjectileMovementComp->ResetInterpolation();
		ProjectileMovementComp->UpdateComponentVelocity();
		ProjectileMovementComp->Activate(true);
		SetActorHiddenInGame(Defaults->IsHidden());
		SetLifeSpan(Defaults->InitialLifeSpan);
		bImpactHandled = false;
	}

	if (IsValid(this) && !bImpactHandled)
	{
		if (ProjectileNiagaraComponent->bAutoActivate) { ProjectileNiagaraComponent->Activate(true); }
		OnProjectileLaunched();
	}
}

void ARPGProjectileBase::SetProjectileFlightAudio(UAudioComponent* Audio)
{
	FlightAudio = Audio;
}

void ARPGProjectileBase::LifeSpanExpired()
{
	EndFlight();
}

void ARPGProjectileBase::EndFlight()
{
	if (!IsValid(this) || IsActorBeingDestroyed())
	{
		return;
	}
	if (OwningPoolName.IsNone() || GetIsReplicated())
	{
		Destroy();
		return;
	}
	bImpactHandled = true;
	SetLifeSpan(0.f);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	ProjectileMovementComp->Deactivate();
	ProjectileMovementComp->SetUpdatedComponent(nullptr);
	ProjectileMovementComp->Velocity = FVector::ZeroVector;
	ProjectileNiagaraComponent->DeactivateImmediate();
	if (UAudioComponent* Audio = FlightAudio.Get()) { Audio->Stop(); }
	FlightAudio.Reset();
	ProjectileDamageEffectSpecHandle.Clear();
	SetOwner(nullptr);
	SetInstigator(nullptr);
	// Movement and ability callbacks can still be unwinding after an impact.
	GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::ReturnToPool);
}

void ARPGProjectileBase::ReturnToPool()
{
	if (!UPoolManager::Get(this)->TryReleaseToPool(OwningPoolName, this, MaxIdleProjectilesPerClass))
	{
		Destroy();
	}
}

void ARPGProjectileBase::OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	ProcessProjectileImpact(OtherActor, OtherComp, Hit);
}

void ARPGProjectileBase::OnProjectileBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (ProjectileDamagePolicy != EProjectileDamagePolicy::OnBeginOverlap)
	{
		return;
	}
	ProcessProjectileImpact(OtherActor, OtherComp, SweepResult);
}

void ARPGProjectileBase::ProcessProjectileImpact(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FHitResult& HitResult)
{
	if (bImpactHandled || !IsValid(OtherActor) || OtherActor == GetInstigator())
	{
		return;
	}
	bImpactHandled = true;

	FVector ImpactPoint(HitResult.ImpactPoint);
	if (ImpactPoint.ContainsNaN() ||
		(ImpactPoint.IsNearlyZero() &&
		 !OtherActor->GetActorLocation().IsNearlyZero()))
	{
		ImpactPoint = OtherActor->GetActorLocation();
	}
	FVector ImpactNormal(HitResult.ImpactNormal);
	if (ImpactNormal.ContainsNaN())
	{
		ImpactNormal = (ImpactPoint - GetActorLocation()).GetSafeNormal();
	}
	const FHitResult ServerHit(
		OtherActor,
		OtherComponent,
		ImpactPoint,
		ImpactNormal);

	BP_OnSpawnProjectileHitFX(ImpactPoint);
	if (!IsValid(this)) { return; }
	if (!HasAuthority() || GetNetMode() == NM_Client)
	{
		// Predicted projectiles only present impact FX. The server copy owns
		// collision admission, block events, damage, and destruction.
		EndFlight();
		return;
	}

	APawn* HitPawn = Cast<APawn>(OtherActor);
	if (!HitPawn ||
		!URPGCombatFunctionLibrary::IsTargetPawnHostile(GetInstigator(), HitPawn))
	{
		EndFlight();
		return;
	}

	const bool bIsPlayerBlocking =
		URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(
			HitPawn,
			RPGGameplayTags::Player_Status_Blocking);
	const bool bIsValidBlock = bIsPlayerBlocking &&
		URPGCombatFunctionLibrary::IsValidBlock(this, HitPawn);

	FGameplayEventData Data;
	Data.Instigator = this;
	Data.Target = HitPawn;
	if (bIsValidBlock)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			HitPawn,
			RPGGameplayTags::Player_Event_SuccessfulBlock,
			Data);
	}
	else
	{
		HandleApplyProjectileDamage(HitPawn, ServerHit, Data);
	}

	EndFlight();
}

void ARPGProjectileBase::HandleApplyProjectileDamage(
	APawn* InHitPawn,
	const FHitResult& ServerHit,
	const FGameplayEventData& InPayload)
{
	if (!HasAuthority() || !IsValid(InHitPawn) ||
		!ProjectileDamageEffectSpecHandle.IsValid())
	{
		return;
	}

	const bool bWasApplied =
		URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
			GetInstigator(),
			ServerHit,
			ProjectileDamageEffectSpecHandle,
			SecurityProfile);

	if (bWasApplied && !RPGCombatImmunity::IsImmune(InHitPawn, ERPGCombatEffectKind::HitReaction))
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			InHitPawn,
			RPGGameplayTags::Shared_Event_HitReact,
			InPayload
		);
	}
}
