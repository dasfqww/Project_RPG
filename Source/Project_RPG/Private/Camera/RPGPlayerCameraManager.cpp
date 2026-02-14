// Fill out your copyright notice in the Description page of Project Settings.


#include "Camera/RPGPlayerCameraManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/RPGCameraComponent.h"
#include "Camera/RPGUICameraManagerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGPlayerCameraManager)

class FDebugDisplayInfo;

ARPGPlayerCameraManager::ARPGPlayerCameraManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultFOV = RPG_CAMERA_DEFAULT_FOV;
	ViewPitchMin = RPG_CAMERA_DEFAULT_PITCH_MIN;
	ViewPitchMax = RPG_CAMERA_DEFAULT_PITCH_MAX;

	UICamera = CreateDefaultSubobject<URPGUICameraManagerComponent>(TEXT("UICamera"));
}

void ARPGPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	if (UICamera->NeedsToUpdateViewTarget())
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		UICamera->UpdateViewTarget(OutVT, DeltaTime);
		return;
	}

	// RPGCameraComponent가 있다면 해당 컴포넌트의 계산 결과를 우선 사용
	if (const APawn* Pawn = Cast<APawn>(OutVT.Target))
	{
		if (URPGCameraComponent* CameraComp = Pawn->FindComponentByClass<URPGCameraComponent>())
		{
			FRPGCameraModeView CameraView;
			CameraComp->GetCameraView(DeltaTime, CameraView);

			OutVT.POV.Location = CameraView.Location;
			OutVT.POV.Rotation = CameraView.Rotation;
			OutVT.POV.FOV = CameraView.FieldOfView;
			return;
		}
	}

	Super::UpdateViewTarget(OutVT, DeltaTime);
}

void ARPGPlayerCameraManager::DisplayDebug(UCanvas* Canvas,
	const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetFont(GEngine->GetSmallFont());
	DisplayDebugManager.SetDrawColor(FColor::Yellow);
	DisplayDebugManager.DrawString(
		FString::Printf(TEXT("LyraPlayerCameraManager: %s"), *GetNameSafe(this)));

	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	const APawn* Pawn = PCOwner ? PCOwner->GetPawn() : nullptr;

	/*if (const ULyraCameraComponent* CameraComponent = ULyraCameraComponent::FindCameraComponent(Pawn))
	{
		CameraComponent->DrawDebug(Canvas);
	}*/
}
