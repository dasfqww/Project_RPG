// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/Player/RPGPlayerLinkedAnimLayer.h"
#include "AnimInstance/Player/RPGPlayerAnimInstance.h"

URPGPlayerAnimInstance* URPGPlayerLinkedAnimLayer::GetPlayerAnimInstance() const
{
	return Cast<URPGPlayerAnimInstance>(GetOwningComponent()->GetAnimInstance());
}
