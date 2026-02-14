// Fill out your copyright notice in the Description page of Project Settings.


#include "Camera/RPGUICameraManagerComponent.h"

#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Camera/RPGPlayerCameraManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGUICameraManagerComponent)


URPGUICameraManagerComponent::URPGUICameraManagerComponent()
{
	bWantsInitializeComponent = true;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Register "showdebug" hook.
		if (!IsRunningDedicatedServer())
		{
			AHUD::OnShowDebugInfo.AddUObject(this, &ThisClass::OnShowDebugInfo);
		}
	}
}

URPGUICameraManagerComponent* URPGUICameraManagerComponent::GetComponent(APlayerController* PC)
{
	if (!IsValid(PC))
	{
		if (ARPGPlayerCameraManager* PCCamera=Cast<ARPGPlayerCameraManager>(PC->PlayerCameraManager))
		{
			//return PCCamera->
		}
	}

	return nullptr;
}

void URPGUICameraManagerComponent::InitializeComponent()
{
	Super::InitializeComponent();

}

void URPGUICameraManagerComponent::SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams)
{
	TGuardValue<bool> SettingViewTargetGuard(bUpdatingViewTarget, true);

	ViewTarget = InViewTarget;

	if (ARPGPlayerCameraManager* PCCamera = Cast<ARPGPlayerCameraManager>(GetOwner()))
	{
		PCCamera->SetViewTarget(InViewTarget, TransitionParams);
	}
}

bool URPGUICameraManagerComponent::NeedsToUpdateViewTarget() const
{
	return false;
}

void URPGUICameraManagerComponent::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
}

void URPGUICameraManagerComponent::OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	/*if (NeedsToUpdateViewTarget())
	{
		FViewTargetTransitionParams TransitionParams;
		TransitionParams.BlendTime = 0.f;
		SetViewTarget(GetViewTarget(), TransitionParams);
	}*/
}