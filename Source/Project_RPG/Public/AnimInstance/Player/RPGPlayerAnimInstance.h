// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstance/RPGCharacterAnimInstance.h"
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

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		bool bShouldEnterRelaxState = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
		float EnterRelaxStateThreshold = 5.f;
	
	float IdleElapsedTime = 0.f;

private:
	friend class FRPGAnimInstanceStateTest;
	void ResetRelaxState();
};
