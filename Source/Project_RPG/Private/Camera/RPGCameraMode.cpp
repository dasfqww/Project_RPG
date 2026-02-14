// Fill out your copyright notice in the Description page of Project Settings.


#include "Camera/RPGCameraMode.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "GameFramework/Character.h"
#include "Camera/RPGCameraComponent.h"
#include "Camera/RPGPlayerCameraManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGCameraMode)

// --- FRPGCameraModeView ---
FRPGCameraModeView::FRPGCameraModeView()
	: Location(FVector::ZeroVector)
	, Rotation(FRotator::ZeroRotator)
	, ControlRotation(FRotator::ZeroRotator)
	, FieldOfView(RPG_CAMERA_DEFAULT_FOV)
{
}

void FRPGCameraModeView::Blend(const FRPGCameraModeView& Other, float OtherWeight)
{
	if (OtherWeight <= 0.0f)
	{
		return;
	}
	else if (OtherWeight >= 1.0f)
	{
		*this = Other;
		return;
	}

	Location = FMath::Lerp(Location, Other.Location, OtherWeight);

	const FRotator DeltaRotation = (Other.Rotation - Rotation).GetNormalized();
	Rotation = Rotation + (OtherWeight * DeltaRotation);

	const FRotator DeltaControlRotation = (Other.ControlRotation - ControlRotation).GetNormalized();
	ControlRotation = ControlRotation + (OtherWeight * DeltaControlRotation);

	FieldOfView = FMath::Lerp(FieldOfView, Other.FieldOfView, OtherWeight);
}

// --- URPGCameraMode ---
URPGCameraMode::URPGCameraMode()
{
	BlendTime = 0.5f;
	BlendFunction = ERPGCameraModeBlendFunction::EaseInOut;
	BlendExponent = 2.0f;
	BlendAlpha = 0.0f;
	BlendWeight = 1.0f;

	FieldOfView = RPG_CAMERA_DEFAULT_FOV;
	ViewPitchMin = RPG_CAMERA_DEFAULT_PITCH_MIN;
	ViewPitchMax = RPG_CAMERA_DEFAULT_PITCH_MAX;
}

UWorld* URPGCameraMode::GetWorld() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? nullptr : GetOuter()->GetWorld();
}

AActor* URPGCameraMode::GetTargetActor() const
{
	if (URPGCameraComponent* CameraComponent = GetCameraComponent())
	{
		return CameraComponent->GetOwner();
	}
	return nullptr;
}

URPGCameraComponent* URPGCameraMode::GetCameraComponent() const
{
	return CastChecked<URPGCameraComponent>(GetOuter());
}

void URPGCameraMode::UpdateCameraMode(float DeltaTime)
{
	UpdateView(DeltaTime);
	UpdateBlending(DeltaTime);
}

void URPGCameraMode::SetBlendWeight(float Weight)
{
	BlendWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
	// 블렌딩 함수에 따른 알파 역산은 필요 시 구현

	const float InExponent = (BlendExponent > 0.0f) ? BlendExponent : 1.0f;

	switch (BlendFunction)
	{
	case ERPGCameraModeBlendFunction::Linear:
		BlendAlpha = BlendWeight;
		break;
	case ERPGCameraModeBlendFunction::EaseIn:
		BlendWeight = FMath::InterpEaseIn(0.0f, 1.0f, BlendAlpha, InExponent);
		break;
	case ERPGCameraModeBlendFunction::EaseOut:
		BlendWeight = FMath::InterpEaseOut(0.0f, 1.0f, BlendAlpha, InExponent);
		break;
	case ERPGCameraModeBlendFunction::EaseInOut:
		BlendWeight = FMath::InterpEaseInOut(0.0f, 1.0f, BlendAlpha, InExponent);
		break;
	}
}

void URPGCameraMode::DrawDebug(UCanvas* Canvas) const
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetDrawColor(FColor::White);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("RPGCameraMode: %s (%f)"), *GetName(), BlendWeight));
}

FVector URPGCameraMode::GetPivotLocation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetPawnViewLocation();
	}

	return TargetActor->GetActorLocation();
}

FRotator URPGCameraMode::GetPivotRotation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetViewRotation();
	}

	return TargetActor->GetActorRotation();
}

void URPGCameraMode::UpdateView(float DeltaTime)
{
	View.Location = GetPivotLocation();
	View.Rotation = GetPivotRotation();
	View.ControlRotation = View.Rotation;
	View.FieldOfView = FieldOfView;
}

void URPGCameraMode::UpdateBlending(float DeltaTime)
{
	if (BlendTime > 0.0f)
	{
		BlendAlpha += (DeltaTime / BlendTime);
		BlendAlpha = FMath::Min(BlendAlpha, 1.0f);
	}
	else
	{
		BlendAlpha = 1.0f;
	}

	const float Exponent = (BlendExponent > 0.0f) ? BlendExponent : 1.0f;

	switch (BlendFunction)
	{
	case ERPGCameraModeBlendFunction::Linear:
		BlendWeight = BlendAlpha;
		break;
	case ERPGCameraModeBlendFunction::EaseIn:
		BlendWeight = FMath::InterpEaseIn(0.0f, 1.0f, BlendAlpha, Exponent);
		break;
	case ERPGCameraModeBlendFunction::EaseOut:
		BlendWeight = FMath::InterpEaseOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;
	case ERPGCameraModeBlendFunction::EaseInOut:
		BlendWeight = FMath::InterpEaseInOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;
	}
}

// --- URPGCameraModeStack ---
URPGCameraModeStack::URPGCameraModeStack()
{
	bIsActive = true;
}

void URPGCameraModeStack::ActivateStack() 
{
	if (!bIsActive)
	{
		bIsActive = true;

		// Notify camera modes that they are being activated.
		for (URPGCameraMode* CameraMode : CameraModeStack)
		{
			check(CameraMode);
			CameraMode->OnActivation();
		}
	}
}
void URPGCameraModeStack::DeactivateStack() 
{
	if (bIsActive)
	{
		bIsActive = false;

		// Notify camera modes that they are being deactivated.
		for (URPGCameraMode* CameraMode : CameraModeStack)
		{
			check(CameraMode);
			CameraMode->OnDeactivation();
		}
	}
}

void URPGCameraModeStack::PushCameraMode(TSubclassOf<URPGCameraMode> CameraModeClass)
{
	if (!CameraModeClass) return;

	URPGCameraMode* CameraMode = GetCameraModeInstance(CameraModeClass);
	check(CameraMode);

	int32 StackSize = CameraModeStack.Num();

	if ((StackSize > 0) && (CameraModeStack[0] == CameraMode))
	{
		// Already top of stack.
		return;
	}

	// See if it's already in the stack and remove it.
	// Figure out how much it was contributing to the stack.
	int32 ExistingStackIndex = INDEX_NONE;
	float ExistingStackContribution = 1.0f;

	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		if (CameraModeStack[StackIndex] == CameraMode)
		{
			ExistingStackIndex = StackIndex;
			ExistingStackContribution *= CameraMode->GetBlendWeight();
			break;
		}
		else
		{
			ExistingStackContribution *= (1.0f - CameraModeStack[StackIndex]->GetBlendWeight());
		}
	}

	if (ExistingStackIndex != INDEX_NONE)
	{
		CameraModeStack.RemoveAt(ExistingStackIndex);
		StackSize--;
	}
	else
	{
		ExistingStackContribution = 0.0f;
	}

	// Decide what initial weight to start with.
	const bool bShouldBlend = ((CameraMode->GetBlendTime() > 0.0f) && (StackSize > 0));
	const float BlendWeight = (bShouldBlend ? ExistingStackContribution : 1.0f);

	CameraMode->SetBlendWeight(BlendWeight);

	// Add new entry to top of stack.
	CameraModeStack.Insert(CameraMode, 0);

	// Make sure stack bottom is always weighted 100%.
	CameraModeStack.Last()->SetBlendWeight(1.0f);

	// Let the camera mode know if it's being added to the stack.
	if (ExistingStackIndex == INDEX_NONE)
	{
		CameraMode->OnActivation();
	}
}

bool URPGCameraModeStack::EvaluateStack(float DeltaTime, FRPGCameraModeView& OutCameraModeView)
{
	if (!bIsActive || CameraModeStack.Num() == 0) return false;

	UpdateStack(DeltaTime);
	BlendStack(OutCameraModeView);

	return true;
}

void URPGCameraModeStack::DrawDebug(UCanvas* Canvas) const
{
	check(Canvas);
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	DisplayDebugManager.SetDrawColor(FColor::Yellow);
	DisplayDebugManager.DrawString(TEXT("--- Camera Modes (Begin) ---"));
	for (const URPGCameraMode* CameraMode : CameraModeStack)
	{
		check(CameraMode);
		CameraMode->DrawDebug(Canvas);
	}

	DisplayDebugManager.SetDrawColor(FColor::Green);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   --- Camera Modes (End) ---")));
}

URPGCameraMode* URPGCameraModeStack::GetCameraModeInstance(TSubclassOf<URPGCameraMode> CameraModeClass)
{
	check(CameraModeClass);

	for (URPGCameraMode* CameraMode : CameraModeInstances)
	{
		if ((CameraMode != nullptr) && (CameraMode->GetClass() == CameraModeClass))
			return CameraMode;
	}

	URPGCameraMode* NewInstance = NewObject<URPGCameraMode>(GetOuter(), CameraModeClass, NAME_None, RF_NoFlags);
	check(NewInstance);

	CameraModeInstances.Add(NewInstance);
	return NewInstance;
}

void URPGCameraModeStack::UpdateStack(float DeltaTime)
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0)
	{
		return;
	}

	int32 RemoveCount = 0;
	int32 RemoveIndex = INDEX_NONE;

	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		URPGCameraMode* CameraMode = CameraModeStack[StackIndex];
		check(CameraMode);

		CameraMode->UpdateCameraMode(DeltaTime);

		if (CameraMode->GetBlendWeight() >= 1.0f)
		{
			// Everything below this mode is now irrelevant and can be removed.
			RemoveIndex = (StackIndex + 1);
			RemoveCount = (StackSize - RemoveIndex);
			break;
		}
	}

	if (RemoveCount > 0)
	{
		// Let the camera modes know they being removed from the stack.
		for (int32 StackIndex = RemoveIndex; StackIndex < StackSize; ++StackIndex)
		{
			URPGCameraMode* CameraMode = CameraModeStack[StackIndex];
			check(CameraMode);

			CameraMode->OnDeactivation();
		}

		CameraModeStack.RemoveAt(RemoveIndex, RemoveCount);
	}
}

void URPGCameraModeStack::BlendStack(FRPGCameraModeView& OutCameraModeView) const
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0)
	{
		return;
	}

	// Start at the bottom and blend up the stack
	const URPGCameraMode* CameraMode = CameraModeStack[StackSize - 1];
	check(CameraMode);

	OutCameraModeView = CameraMode->GetCameraModeView();

	for (int32 StackIndex = (StackSize - 2); StackIndex >= 0; --StackIndex)
	{
		CameraMode = CameraModeStack[StackIndex];
		check(CameraMode);

		OutCameraModeView.Blend(CameraMode->GetCameraModeView(), CameraMode->GetBlendWeight());
	}
}

void URPGCameraModeStack::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
	if (CameraModeStack.Num() == 0)
	{
		OutWeightOfTopLayer = 1.0f;
		OutTagOfTopLayer = FGameplayTag();
		return;
	}
	else
	{
		URPGCameraMode* TopEntry = CameraModeStack.Last();
		check(TopEntry);
		OutWeightOfTopLayer = TopEntry->GetBlendWeight();
		OutTagOfTopLayer = TopEntry->GetCameraTypeTag();
	}
}
