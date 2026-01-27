// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "GameplayTagContainer.h"
#include "PawnCombatComponent.generated.h"

class ARPGWeaponBase;

UENUM(BlueprintType)
enum class EToggleDamageType : uint8
{
	CurrentEquippedWeapon,
	LeftHand,
	RightHand,
	Body
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UPawnCombatComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		void RegisterSpawnedWeapon(FGameplayTag InWeaponTagToRegister,
			ARPGWeaponBase* InWeaponToRegister, bool bRegisterAsEquippedWeapon = false);

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		ARPGWeaponBase* GetCharacterHasWeaponByTag(FGameplayTag InWeaponTagToGet) const;

	UPROPERTY(BlueprintReadWrite, Category = "RPG|Combat")
		FGameplayTag CurrentEquippedWeaponTag;

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		ARPGWeaponBase* GetCharacterCurrentEquippedWeapon() const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
	void ToggleWeaponCollision(bool bShouldEnable, EToggleDamageType
			ToggleDamageType = EToggleDamageType::CurrentEquippedWeapon);


	virtual void OnHitTargetActor(AActor* HitActor);
	virtual void OnWeaponPulledFromTargetActor(AActor* InteractedActor);

	virtual void ToggleCurrentEquippedWeaponCollision(bool bShouldEnable);
	virtual void ToggleBodyCollsionBoxCollision(bool bShouldEnable, EToggleDamageType ToggleDamageType);

protected:
	TArray<AActor*> OverlappedActors;

private:
	TMap<FGameplayTag, ARPGWeaponBase*> CharacterHasWeaponMap;
};
