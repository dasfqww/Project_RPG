// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/RPGInputComponent.h"

void URPGInputComponent::ExecuteReflectionHandler(const FInputActionValue& Value, FName FuncName, UObject* ContextObject)
{
	if (ContextObject)
	{
		// 대상 함수를 리플렉션으로 직접 호출 (인자 전달 포함)
		// 주의: 호출될 함수는 반드시 UFUNCTION()이어야 하며, const FInputActionValue& 인자를 받아야 합니다.
		ContextObject->ProcessEvent(ContextObject->FindFunction(FuncName), (void*)&Value);
	}
}
