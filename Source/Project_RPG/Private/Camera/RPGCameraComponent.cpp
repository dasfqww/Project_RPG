// Fill out your copyright notice in the Description page of Project Settings.


#include "Camera/RPGCameraComponent.h"

URPGCameraComponent::URPGCameraComponent()
{
}

void URPGCameraComponent::OnRegister()
{
	Super::OnRegister();

	if (!CameraModeStack)
	{
		CameraModeStack = NewObject<URPGCameraModeStack>(this);
	}

	if (DefaultCameraMode)
	{
		PushCameraMode(DefaultCameraMode);
	}
}

void URPGCameraComponent::GetCameraView(float DeltaTime, FRPGCameraModeView& DesiredView)
{
	check(CameraModeStack);
	CameraModeStack->EvaluateStack(DeltaTime, DesiredView);
}

void URPGCameraComponent::PushCameraMode(TSubclassOf<URPGCameraMode> CameraModeClass)
{
	if (CameraModeStack)
	{
		CameraModeStack->PushCameraMode(CameraModeClass);
	}
}
