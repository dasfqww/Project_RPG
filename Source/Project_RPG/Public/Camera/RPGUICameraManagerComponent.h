// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGUICameraManagerComponent.generated.h"

class ARPGPlayerCameraManager;

class AActor;
class AHUD;
class APlayerController;
class FDebugDisplayInfo;
class UCanvas;
class UObject;

UCLASS(Transient, Within = RPGPlayerCameraManager)
class PROJECT_RPG_API URPGUICameraManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	URPGUICameraManagerComponent();

	static URPGUICameraManagerComponent* GetComponent(APlayerController* PC);

	virtual void InitializeComponent() override;

	
	void SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());

	bool NeedsToUpdateViewTarget() const;
	void UpdateViewTarget(struct FTViewTarget& OutVT, float DeltaTime);

	void OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> ViewTarget;

	UPROPERTY(Transient)
	bool bUpdatingViewTarget;

public:	
	FORCEINLINE bool IsSettingViewTarget() const { return bUpdatingViewTarget; }
	FORCEINLINE AActor* GetViewTarget() const { return ViewTarget; }
		
};
