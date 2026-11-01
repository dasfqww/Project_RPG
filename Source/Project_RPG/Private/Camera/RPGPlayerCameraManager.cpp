// Fill out your copyright notice in the Description page of Project Settings.


#include "Camera/RPGPlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/RPGCameraComponent.h"
#include "Camera/RPGUICameraManagerComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGPlayerCameraManager)

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
	if (IsValid(UICamera) && UICamera->NeedsToUpdateViewTarget())
	{
		// Match the engine's outgoing-view lock before calculating an optional presentation.
		if (PendingViewTarget.Target && BlendParams.bLockOutgoing && OutVT.Equal(ViewTarget)) { return; }
		TStrongObjectPtr<ARPGPlayerCameraManager> KeepAlive(this);
		const TWeakObjectPtr<APlayerController> ExpectedController(GetOwningPlayerController());
		const TWeakObjectPtr<UWorld> ExpectedWorld(GetWorld());
		const auto HasCurrentOwner = [&]()
		{
			return !IsActorBeingDestroyed() && !IsUnreachable()
				&& !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
				&& ExpectedWorld.IsValid() && !ExpectedWorld->bIsTearingDown && GetWorld() == ExpectedWorld.Get()
				&& ExpectedController.IsValid() && !ExpectedController->IsActorBeingDestroyed()
				&& GetOwningPlayerController() == ExpectedController.Get()
				&& ExpectedController->GetWorld() == ExpectedWorld.Get() && ExpectedController->PlayerCameraManager == this;
		};
		if (!HasCurrentOwner()) { return; }
		const bool bAppliedPresentation = UICamera->TryUpdateViewTarget(OutVT, DeltaTime);
		if (!HasCurrentOwner()) { return; }
		if (bAppliedPresentation)
		{
			const ACameraActor* PresentationCamera = UICamera->GetPresentationViewTarget();
			const FVector PresentationOrigin = IsValid(PresentationCamera)
				? PresentationCamera->GetActorLocation() : OutVT.POV.Location;
			// Run the engine's camera-actor post processing on the selected POV, rather than on the discarded pawn view.
			ApplyCameraModifiers(DeltaTime, OutVT.POV);
			if (!HasCurrentOwner()) { return; }
			SetActorLocationAndRotation(OutVT.POV.Location, OutVT.POV.Rotation, false);
			if (!HasCurrentOwner()) { return; }
			if (bAutoCalculateOrthoPlanes) { OutVT.POV.SetCameraToViewTarget(PresentationOrigin); }
			UpdateCameraLensEffects(OutVT);
			return;
		}
		// An invalidated request falls through to the existing pawn/engine path in this same frame.
	}

	// RPGCameraComponent가 있다면 해당 컴포넌트의 계산 결과를 우선 사용
	if (const APawn* Pawn = Cast<APawn>(OutVT.Target))
	{
		if (URPGCameraComponent* CameraComp = Pawn->FindComponentByClass<URPGCameraComponent>())
		{
			FMinimalViewInfo MinimalViewInfo;
			CameraComp->GetCameraView(DeltaTime, MinimalViewInfo);

			OutVT.POV.Location = MinimalViewInfo.Location;
			OutVT.POV.Rotation = MinimalViewInfo.Rotation;
			OutVT.POV.FOV = MinimalViewInfo.FOV;
			OutVT.POV.PostProcessBlendWeight = MinimalViewInfo.PostProcessBlendWeight;
			OutVT.POV.PostProcessSettings = MinimalViewInfo.PostProcessSettings;
			// 필요에 따라 추가 필드 복사

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
		FString::Printf(TEXT("RPGPlayerCameraManager: %s"), *GetNameSafe(this)));

	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	const APawn* Pawn = IsValid(PCOwner) ? PCOwner->GetPawn() : nullptr;
	if (const URPGCameraComponent* CameraComponent = URPGCameraComponent::FindCameraComponent(Pawn))
	{
		if (IsValid(CameraComponent) && CameraComponent->IsRegistered())
		{
			CameraComponent->DrawDebug(Canvas);
		}
	}
}
