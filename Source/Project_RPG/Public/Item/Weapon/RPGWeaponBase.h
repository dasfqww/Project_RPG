// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Type/RPGEnumTypes.h"
#include "RPGWeaponBase.generated.h"

class UBoxComponent;
class UNiagaraSystem;
class UStaticMeshComponent;

DECLARE_DELEGATE_OneParam(FOnTargetInteractedDelegate, AActor*)

UCLASS()
class PROJECT_RPG_API ARPGWeaponBase : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ARPGWeaponBase();
	
	FOnTargetInteractedDelegate OnWeaponHitTarget;
	FOnTargetInteractedDelegate OnWeaponPulledFromTarget;

protected:
	/*UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
		TObjectPtr<USkeletalMeshComponent> WeaponSkeletalMesh;*/

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
		TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
		TObjectPtr<UBoxComponent> WeaponCollisionBox;
	
	UPROPERTY(EditDefaultsOnly, Category = "Effect")
	TObjectPtr<UNiagaraSystem> HitEffect;

	/** Optional strict D1 identity. Count keeps legacy weapon actors on the compatibility path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapons|D1 Compatibility")
	ERPGGladiatorWeaponType WeaponType = ERPGGladiatorWeaponType::Count;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapons|D1 Compatibility")
	EWeaponHandType WeaponHandType = EWeaponHandType::Count;

	UFUNCTION()
		virtual void OnCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
			UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
		virtual void OnCollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
			UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

public:
	FORCEINLINE UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }
	FORCEINLINE UBoxComponent* GetWeaponCollisionBox() const { return WeaponCollisionBox; }
	FORCEINLINE ERPGGladiatorWeaponType GetWeaponType() const { return WeaponType; }
	FORCEINLINE EWeaponHandType GetWeaponHandType() const { return WeaponHandType; }
};
