// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "RPGPlayerCameraManager.generated.h"

class FDebugDisplayInfo;
class UCanvas;
class UObject;

#define RPG_CAMERA_DEFAULT_FOV			(+80.0f)
#define RPG_CAMERA_DEFAULT_PITCH_MIN	(-75.0f)
#define RPG_CAMERA_DEFAULT_PITCH_MAX	(+55.0f)

class URPGUICameraManagerComponent;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
public:
	ARPGPlayerCameraManager(const FObjectInitializer& ObjectInitializer);

	

protected:

	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	virtual void DisplayDebug(UCanvas* Canvas, 
		const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;

private:
	/** The UI Camera Component, controls the camera when UI is doing something important that gameplay doesn't get priority over. */
	UPROPERTY(Transient)
	TObjectPtr<URPGUICameraManagerComponent> UICamera;
public:
	FORCEINLINE URPGUICameraManagerComponent* GetUICameraComponent() const { return UICamera; }
};
