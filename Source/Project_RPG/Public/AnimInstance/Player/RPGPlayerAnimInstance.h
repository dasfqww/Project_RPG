// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstance/RPGCharacterAnimInstance.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "RPGPlayerAnimInstance.generated.h"

class ARPGPlayer;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGPlayerAnimInstance : public URPGCharacterAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeUninitializeAnimation() override;

protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|References")
		TObjectPtr<ARPGPlayer> OwningPlayerCharacter;

	// Captured together on the game thread; graph evaluation never queries combat state.
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Weapon")
		bool bHasActiveWeapon = false;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Weapon")
		ERPGWeaponCarryState WeaponCarryState = ERPGWeaponCarryState::Stowed;

	/** Keep the armed pose until the sheathing notify actually stows the weapon. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Weapon")
		bool bUseArmedLocomotion = false;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		bool bShouldEnterRelaxState = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		float EnterRelaxStateThreshold = 5.f;
	
	float IdleElapsedTime = 0.f;

private:
	friend class FRPGAnimInstanceStateTest;
	friend class FRPGAnimWeaponPresentationTest;
	void ResetRelaxState();
	void ResetWeaponState();
};
