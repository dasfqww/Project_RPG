#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "RPGCameraMode.generated.h"

class AActor;
class UCanvas;
class URPGCameraComponent;

/**
 * ERPGCameraModeBlendFunction
 *
 *	Blend function used for transitioning between camera modes.
 */
UENUM(BlueprintType)
enum class ERPGCameraModeBlendFunction : uint8
{
	// Does a simple linear interpolation.
	Linear,

	// Immediately accelerates, but smoothly decelerates into the target.
	EaseIn,

	// Smoothly accelerates, but does not decelerate into the target.
	EaseOut,

	// Smoothly accelerates and decelerates.
	EaseInOut,

	COUNT	UMETA(Hidden)
};


/**
 * FRPGCameraModeView
 *
 *	View data produced by the camera mode that is used to blend camera modes.
 */
USTRUCT(BlueprintType)
struct FRPGCameraModeView
{
	GENERATED_BODY()

public:

	FRPGCameraModeView();

	void Blend(const FRPGCameraModeView& Other, float OtherWeight);

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	FVector Location;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	FRotator Rotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	FRotator ControlRotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	float FieldOfView;
};


/**
 * URPGCameraMode
 *
 *	Base class for all camera modes.
 */
UCLASS(Abstract, Blueprintable)
class PROJECT_RPG_API URPGCameraMode : public UObject
{
	GENERATED_BODY()

public:

	URPGCameraMode();

	virtual UWorld* GetWorld() const override;

	AActor* GetTargetActor() const;

	URPGCameraComponent* GetCameraComponent() const;

	virtual void OnActivation() {};
	virtual void OnDeactivation() {};

	void UpdateCameraMode(float DeltaTime);
	
	void SetBlendWeight(float Weight);

	virtual void DrawDebug(UCanvas* Canvas) const;

protected:

	virtual FVector GetPivotLocation() const;
	virtual FRotator GetPivotRotation() const;

	virtual void UpdateView(float DeltaTime);
	virtual void UpdateBlending(float DeltaTime);

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	FGameplayTag CameraTypeTag;

	UPROPERTY(EditDefaultsOnly, Category = "View")
	float FieldOfView;

	UPROPERTY(EditDefaultsOnly, Category = "View")
	float ViewPitchMin;

	UPROPERTY(EditDefaultsOnly, Category = "View")
	float ViewPitchMax;

	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendTime;

	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	ERPGCameraModeBlendFunction BlendFunction;

	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendExponent;

	float BlendAlpha;
	float BlendWeight;

	FRPGCameraModeView View;

public:
	FORCEINLINE float GetBlendTime() const { return BlendTime; }
	FORCEINLINE float GetBlendWeight() const { return BlendWeight; }
	FORCEINLINE const FGameplayTag& GetCameraTypeTag() const { return CameraTypeTag; }
	FORCEINLINE const FRPGCameraModeView& GetCameraModeView() const { return View; }
};


/**
 * URPGCameraModeStack
 *
 *	Stack used for blending camera modes.
 */
UCLASS()
class URPGCameraModeStack : public UObject
{
	GENERATED_BODY()

public:

	URPGCameraModeStack();

	void ActivateStack();
	void DeactivateStack();

	bool IsStackActive() const { return bIsActive; }

	void PushCameraMode(TSubclassOf<URPGCameraMode> CameraModeClass);

	bool EvaluateStack(float DeltaTime, FRPGCameraModeView& OutCameraModeView);

	void DrawDebug(UCanvas* Canvas) const;

	void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

protected:

	URPGCameraMode* GetCameraModeInstance(TSubclassOf<URPGCameraMode> CameraModeClass);

	void UpdateStack(float DeltaTime);
	void BlendStack(FRPGCameraModeView& OutCameraModeView) const;

protected:

	bool bIsActive;

	UPROPERTY()
	TArray<TObjectPtr<URPGCameraMode>> CameraModeInstances;

	UPROPERTY()
	TArray<TObjectPtr<URPGCameraMode>> CameraModeStack;
};
