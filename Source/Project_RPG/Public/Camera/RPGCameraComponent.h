// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "Camera/RPGCameraMode.h"
#include "RPGCameraComponent.generated.h"

class URPGCameraModeStack;

/**
 * Lyra 스타일의 스택 기반 카메라 컴포넌트
 */
UCLASS(Blueprintable, meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	URPGCameraComponent();

	virtual void OnRegister() override;

	/** PlayerCameraManager에서 호출하여 최종 카메라 뷰를 계산 */
	void GetCameraView(float DeltaTime, FRPGCameraModeView& DesiredView);

	/** 새로운 카메라 모드를 스택에 푸시 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Camera")
	void PushCameraMode(TSubclassOf<URPGCameraMode> CameraModeClass);

protected:
	/** 카메라 모드들을 관리하는 스택 */
	UPROPERTY()
	TObjectPtr<URPGCameraModeStack> CameraModeStack;

	/** 기본적으로 사용할 카메라 모드 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	TSubclassOf<URPGCameraMode> DefaultCameraMode;
};
