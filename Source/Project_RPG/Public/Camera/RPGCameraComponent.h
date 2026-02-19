// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "Camera/RPGCameraMode.h"

#include "RPGCameraComponent.generated.h"

class UCanvas;
class URPGCameraMode;
class URPGCameraModeStack;
struct FGameplayTag;
struct FMinimalViewInfo;

/**
 * URPGCameraComponent
 *
 *	이 프로젝트에서 사용하는 베이스 카메라 컴포넌트 클래스입니다. (Lyra 기반)
 */
DECLARE_DELEGATE_RetVal(TSubclassOf<URPGCameraMode>, FRPGCameraModeDelegate);

UCLASS(Blueprintable, meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:

	URPGCameraComponent(const FObjectInitializer& ObjectInitializer);

	// 지정된 액터에서 RPG 카메라 컴포넌트를 찾아 반환합니다.
	UFUNCTION(BlueprintPure, Category = "RPG|Camera")
	static URPGCameraComponent* FindCameraComponent(const AActor* Actor) { return (Actor ? Actor->FindComponentByClass<URPGCameraComponent>() : nullptr); }

	// 카메라가 바라보고 있는 타겟 액터를 반환합니다.
	virtual AActor* GetTargetActor() const { return GetOwner(); }

	// 최적의 카메라 모드를 쿼리하기 위한 델리게이트입니다.
	FRPGCameraModeDelegate DetermineCameraModeDelegate;

	// Field of View에 오프셋을 추가합니다. (한 프레임 동안만 유지)
	void AddFieldOfViewOffset(float FovOffset) { FieldOfViewOffset += FovOffset; }

	virtual void DrawDebug(UCanvas* Canvas) const;

	// 최상단 레이어의 태그와 블렌드 웨이트 정보를 가져옵니다.
	void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

	/** 새로운 카메라 모드를 스택에 푸시 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Camera")
	void PushCameraMode(TSubclassOf<URPGCameraMode> CameraModeClass);

	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
protected:

	virtual void OnRegister() override;
	
	virtual void UpdateCameraModes();

protected:

	// 카메라 모드들을 블렌딩하기 위한 스택입니다.
	UPROPERTY()
	TObjectPtr<URPGCameraModeStack> CameraModeStack;

	// Field of View에 적용될 오프셋입니다.
	float FieldOfViewOffset;
};
