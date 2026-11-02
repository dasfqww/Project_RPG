// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstance/RPGBaseAnimInstance.h"
#include "RPGPlayerLinkedAnimLayer.generated.h"

class URPGPlayerAnimInstance;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGPlayerLinkedAnimLayer : public URPGBaseAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeUninitializeAnimation() override;

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe))
		URPGPlayerAnimInstance* GetPlayerAnimInstance() const;

private:
	void RefreshPlayerAnimInstance();
	UPROPERTY(Transient)
	TObjectPtr<URPGPlayerAnimInstance> CachedPlayerAnimInstance;
};
