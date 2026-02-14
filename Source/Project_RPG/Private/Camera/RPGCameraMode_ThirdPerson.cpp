// Fill out your copyright notice in the Description page of Project Settings.


#include "Camera/RPGCameraMode_ThirdPerson.h"
#include "GameFramework/Character.h"

URPGCameraMode_ThirdPerson::URPGCameraMode_ThirdPerson()
{
	TargetArmLength = 400.0f;
	TargetOffset = FVector(0.0f, 0.0f, 50.0f);
}

void URPGCameraMode_ThirdPerson::UpdateView(float DeltaTime)
{
	FVector PivotLocation = GetPivotLocation() + TargetOffset;
	FRotator PivotRotation = GetPivotRotation();

	// 회전값에 따른 뒤쪽 방향 벡터 계산
	FVector CameraDirection = PivotRotation.Vector() * -1.0f;
	
	// 최종 위치 = 피벗 위치 + (방향 * 팔 길이)
	View.Location = PivotLocation + (CameraDirection * TargetArmLength);
	View.Rotation = PivotRotation;
	View.ControlRotation = PivotRotation;
	View.FieldOfView = FieldOfView;

	// TODO: 장애물 충돌 처리(Camera Collision/SpringArm 효과)를 원한다면 
	// 여기에 간단한 LineTrace 로직을 추가할 수 있습니다.
}
