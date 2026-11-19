// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGProjectileBase.generated.h"

class UBoxComponent;
class UAudioComponent;
class UNiagaraComponent;
class UProjectileMovementComponent;
struct FGameplayEventData;

UENUM(BlueprintType)
enum class EProjectileDamagePolicy : uint8
{
	OnHit,
	OnBeginOverlap
};

USTRUCT()
struct FRPGProjectileFlightState
{
	GENERATED_BODY()

	/** A new launch is observable even if the idle update was coalesced. */
	UPROPERTY()
	uint32 Sequence = 0;

	UPROPERTY()
	bool bActive = false;
};

UCLASS()
class PROJECT_RPG_API ARPGProjectileBase : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ARPGProjectileBase();

	/** Starts an authoritative flight. Classes authored to replicate use their regular spawn path. */
	UFUNCTION(BlueprintCallable, Category = "Projectile")
	static ARPGProjectileBase* LaunchPooledProjectile(TSubclassOf<ARPGProjectileBase> ProjectileClass,
		FVector Location, FRotator Rotation, APawn* ShootingPawn, FGameplayEffectSpecHandle DamageEffectSpec);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void LifeSpanExpired() override;
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Per-flight presentation, including the first launch. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Projectile")
	void OnProjectileLaunched();

	UFUNCTION(BlueprintCallable, Category = "Projectile")
	void SetProjectileFlightAudio(UAudioComponent* Audio);

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
		TObjectPtr<UBoxComponent> ProjectileCollisionBox;
	
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
		TObjectPtr<UNiagaraComponent> ProjectileNiagaraComponent;
	
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
		TObjectPtr<UProjectileMovementComponent> ProjectileMovementComp;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
		EProjectileDamagePolicy ProjectileDamagePolicy = EProjectileDamagePolicy::OnHit;

	UPROPERTY(BlueprintReadOnly, Category = "Projectile", meta = (ExposeOnSpawn = "true"))
		FGameplayEffectSpecHandle ProjectileDamageEffectSpecHandle;

	/** Server collision, damage, and range limits for this projectile family. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Security",
		meta = (ShowOnlyInnerProperties))
	FRPGSkillSecurityProfile SecurityProfile;

	UFUNCTION()
		virtual void OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
			UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
		virtual void OnProjectileBeginOverlap(UPrimitiveComponent* OverlappedComponent, 
			AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
			bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Spawn Projectile Hit FX"))
		void BP_OnSpawnProjectileHitFX(const FVector& HitLocation);

private:
	void Relaunch(const FVector& Location, const FRotator& Rotation, APawn* ShootingPawn,
		const FGameplayEffectSpecHandle& DamageEffectSpec);
	void EndFlight();
	void ReturnToPool();
	void StopFlightPresentation();

	UFUNCTION()
	void OnRep_NetworkFlight();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFlightImpact(FVector_NetQuantize ImpactPoint);

	void ProcessProjectileImpact(
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		const FHitResult& HitResult);
	void HandleApplyProjectileDamage(
		APawn* InHitPawn,
		const FHitResult& ServerHit,
		const FGameplayEventData& InPayload);

	bool bImpactHandled = false;
	FName OwningPoolName;
	TWeakObjectPtr<UAudioComponent> FlightAudio;

	UPROPERTY(ReplicatedUsing = OnRep_NetworkFlight)
	FRPGProjectileFlightState NetworkFlight;
};
