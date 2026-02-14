// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/RPGCameraMode.h"
#include "RPGCameraMode_ThirdPerson.generated.h"

/**
 * 전형적인 3인칭 카메라 모드 (SpringArm 로직 포함)
 */
UCLASS()
class PROJECT_RPG_API URPGCameraMode_ThirdPerson : public URPGCameraMode
{
	GENERATED_BODY()

public:
	URPGCameraMode_ThirdPerson();

protected:
	virtual void UpdateView(float DeltaTime) override;

protected:
	/** 캐릭터로부터의 거리 */
	UPROPERTY(EditDefaultsOnly, Category = "Third Person")
	float TargetArmLength;

	/** 캐릭터 기준 위치 오프셋 (어깨 위 등) */
	UPROPERTY(EditDefaultsOnly, Category = "Third Person")
	FVector TargetOffset;
};
