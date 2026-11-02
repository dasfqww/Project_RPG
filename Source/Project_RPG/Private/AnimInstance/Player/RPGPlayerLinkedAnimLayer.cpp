// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/Player/RPGPlayerLinkedAnimLayer.h"
#include "AnimInstance/Player/RPGPlayerAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void URPGPlayerLinkedAnimLayer::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	RefreshPlayerAnimInstance();
}

void URPGPlayerLinkedAnimLayer::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	RefreshPlayerAnimInstance();
}

void URPGPlayerLinkedAnimLayer::NativeUninitializeAnimation()
{
	CachedPlayerAnimInstance = nullptr;
	Super::NativeUninitializeAnimation();
}

void URPGPlayerLinkedAnimLayer::RefreshPlayerAnimInstance()
{
	const USkeletalMeshComponent* Mesh = GetOwningComponent();
	CachedPlayerAnimInstance = Mesh ? Cast<URPGPlayerAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
}

URPGPlayerAnimInstance* URPGPlayerLinkedAnimLayer::GetPlayerAnimInstance() const
{
	return CachedPlayerAnimInstance;
}
