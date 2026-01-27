// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/Combat/PawnCombatComponent.h"

#include "PlayerCombatComponent.generated.h"

class ARPGPlayerWeapon;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UPlayerCombatComponent : public UPawnCombatComponent
{
	GENERATED_BODY()
public:	
	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		ARPGPlayerWeapon* GetPlayerHasWeaponByTag(FGameplayTag InWeaponTag) const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		ARPGPlayerWeapon* GetPlayerCurrentEquippedWeapon() const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		float GetPlayerCurrentEquipWeaponDamageAtLevel(float InLevel) const;

protected:
	virtual void OnHitTargetActor(AActor* HitActor);
	virtual void OnWeaponPulledFromTargetActor(AActor* InteractedActor);

	void StartManaRecovery();
	bool IsManaFull() const;
	
	void StopManaRecovery();

	void RecoverMana();

	virtual void BeginPlay() override;
private:
	UPROPERTY(EditDefaultsOnly, Category = "Mana", meta = (AllowPrivateAccess="true"))
	float ManaRecoveryRatio = 0.1f;

	FTimerHandle ManaRecoveryTimerHandle;
	bool bIsManaRecoveryActive;
};
