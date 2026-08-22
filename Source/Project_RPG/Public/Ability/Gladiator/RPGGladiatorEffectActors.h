#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGGladiatorEffectActors.generated.h"

class UArrowComponent;
class UCameraShakeBase;
class UCapsuleComponent;
class UCurveFloat;
class UGameplayEffect;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTimelineComponent;

namespace RPGGladiatorEffectActors
{
	/** Shared D1-compatible damage path used by projectiles, AOE elements, and class skills. */
	PROJECT_RPG_API bool ApplyDamage(AActor* SourceActor, AActor* TargetActor,
		const FHitResult& HitResult, float Damage,
		TSubclassOf<UGameplayEffect> AdditionalEffect = nullptr,
		AActor* EffectCauser = nullptr,
		const FRPGSkillSecurityProfile& SecurityProfile =
			FRPGSkillSecurityProfile());
}

UENUM(BlueprintType)
enum class ERPGGladiatorCollisionDetectionType : uint8
{
	None,
	Hit,
	Overlap
};

/** Projectile base retaining the component and property names serialized by D1. */
UCLASS(BlueprintType, Abstract)
class PROJECT_RPG_API ARPGGladiatorProjectileBase : public AActor
{
	GENERATED_BODY()

public:
	ARPGGladiatorProjectileBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

	UFUNCTION(BlueprintCallable)
	void SetSpeed(float Speed);

	UFUNCTION()
	void HandleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& HitResult);

	UFUNCTION()
	void HandleComponentOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleOtherComponentDeactivated(UActorComponent* OtherComponent);

	void HandleCollisionDetection(AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		const FHitResult& HitResult);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float Damage = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "RPG|Gladiator|Projectile|Security",
		meta = (ShowOnlyInnerProperties))
	FRPGSkillSecurityProfile SecurityProfile;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "GameplayCue"))
	FGameplayTag HitGameplayCueTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UNiagaraSystem> HitEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bAttachToHitComponent = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	ERPGGladiatorCollisionDetectionType CollisionDetectionType =
		ERPGGladiatorCollisionDetectionType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<USphereComponent> SphereCollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMeshComponent> ProjectileMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UNiagaraComponent> TrailNiagaraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UProjectileMovementComponent> ProjectileMovementComponent;

private:
	UPROPERTY()
	TWeakObjectPtr<UActorComponent> AttachingComponent;

	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> HitActors;

	int32 ServerAppliedHitCount = 0;
};

UCLASS(BlueprintType, Abstract)
class PROJECT_RPG_API ARPGGladiatorAOEBase : public AActor
{
	GENERATED_BODY()

public:
	ARPGGladiatorAOEBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> ArrowComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> SphereComponent;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TSubclassOf<AActor> AOEElementClass;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	float StartDelay = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	float AttackTotalTime = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	int32 TargetAttackCount = 5;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

private:
	void StartAOE();
	void TickAOE();

	int32 CurrentAttackCount = 0;
	float AttackIntervalTime = 0.0f;
	FTimerHandle AOETimerHandle;
};

UENUM(BlueprintType)
enum class ERPGGladiatorAOEElementType : uint8
{
	Projectile,
	Explosion
};

UCLASS()
class PROJECT_RPG_API ARPGGladiatorAOEElementBase : public AActor
{
	GENERATED_BODY()

public:
	ARPGGladiatorAOEElementBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTimelineTick(float Value);

	UFUNCTION()
	void OnTimelineFinished();

	bool PerformTrace(const FVector& StartLocation, const FVector& EndLocation,
		TArray<FHitResult>& OutHitResults);
	void HandleCollisionDetection(const FHitResult& HitResult);

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE Element")
	float Damage = 40.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE Element|Security",
		meta = (ShowOnlyInnerProperties))
	FRPGSkillSecurityProfile SecurityProfile;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE Element")
	TSubclassOf<UGameplayEffect> AdditionalEffect;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE Element")
	ERPGGladiatorAOEElementType ElementType = ERPGGladiatorAOEElementType::Projectile;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE Element")
	TObjectPtr<UCurveFloat> CurveData;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE Element")
	TObjectPtr<UNiagaraSystem> HitNiagaraEffect;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> ArrowComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> NiagaraComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTimelineComponent> TimelineComponent;

private:
	TSet<TWeakObjectPtr<AActor>> ServerAppliedActors;
};
