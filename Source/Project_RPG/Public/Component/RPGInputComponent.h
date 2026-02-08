// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "GameplayTagContainer.h"
#include "RPGInputComponent.generated.h"

/** 
 * 범용 입력 핸들러 델리게이트 
 */
DECLARE_DELEGATE_OneParam(FRPGInputHandlerSignature, const FInputActionValue&);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()
	
public:
	
	template<class UserObject, typename CallbackFunc>
	void BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig, const FGameplayTag& InInputTag, 
		ETriggerEvent TriggerEvent, UserObject* ContextObject, CallbackFunc Func);

	template<class UserObject, typename CallbackFunc, typename... VarTypes>
	void BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig, const FGameplayTag& InInputTag,
		ETriggerEvent TriggerEvent, UserObject* ContextObject, CallbackFunc Func, VarTypes... Vars);

	template<class UserObject, typename CallbackFunc>
	void BindAbilityInputAction(const UDataAsset_InputConfig* InInputConfig, UserObject* ContextObject,
		CallbackFunc InputPressedFunc, CallbackFunc InputRelasedFunc);

	/**
	 * [완전 자동화] 리플렉션을 이용해 데이터 에셋의 모든 입력을 자동으로 바인딩합니다.
	 * 규칙: 태그명이 'InputTag.Jump'라면 클래스 내의 'Input_Jump' 함수를 찾아 바인딩합니다.
	 */
	template<class UserObject>
	void BindNativeInputActions(const UDataAsset_InputConfig* InInputConfig, UserObject* ContextObject);

private:
	/** 리플렉션으로 호출될 핸들러 함수들 (모든 입력은 이 범용 핸들러를 거쳐 실제 함수로 전달됨) */
	void ExecuteReflectionHandler(const FInputActionValue& Value, FName FuncName, UObject* ContextObject);
};

// --- 구현부 ---

template<class UserObject>
inline void URPGInputComponent::BindNativeInputActions(const UDataAsset_InputConfig* InInputConfig, UserObject* ContextObject)
{
	checkf(InInputConfig, TEXT("Input config data asset is null"));

	for (const FWarriorInputActionConfig& Config : InInputConfig->NativeInputActions)
	{
		if (!Config.IsValid()) continue;

		// 1. 태그로부터 함수 이름 생성 (예: InputTag.Move -> Input_Move)
		FString TagString = Config.InputTag.ToString();
		FString FuncNameStr = TagString.Replace(TEXT("InputTag."), TEXT("Input_"));
		FName FuncName = FName(*FuncNameStr);

		// 2. 대상 오브젝트(Controller 등)에 해당 함수가 존재하는지 리플렉션으로 확인
		UFunction* TargetFunc = ContextObject->FindFunction(FuncName);
		
		if (TargetFunc)
		{
			// 3. 함수가 존재하면 바인딩 (범용 래퍼 함수 이용)
			BindAction(Config.InputAction, ETriggerEvent::Triggered, this, &ThisClass::ExecuteReflectionHandler, FuncName, (UObject*)ContextObject);
		}
		else
		{
			// 함수를 못 찾았을 경우 로그 (개발자 실수 방지)
			UE_LOG(LogTemp, Warning, TEXT("Input Automation: Could not find UFUNCTION '%s' in class '%s' for tag '%s'"), 
				*FuncNameStr, *ContextObject->GetClass()->GetName(), *TagString);
		}
	}
}

//�ݹ��Լ� �Ķ����X
template<class UserObject, typename CallbackFunc>
inline void URPGInputComponent::BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig,
	const FGameplayTag& InInputTag, ETriggerEvent TriggerEvent, UserObject* ContextObject, CallbackFunc Func)
{
	checkf(InInputConfig, TEXT("Input config data asset is null,can not proceed with binding"));

	if (UInputAction* FoundAction = InInputConfig->FindNativeInputActionByTag(InInputTag))
	{
		BindAction(FoundAction, TriggerEvent, ContextObject, Func);
	}
}

//�ݹ��Լ� �Ķ����O
template<class UserObject, typename CallbackFunc, typename ...VarTypes>
inline void URPGInputComponent::BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig, 
	const FGameplayTag& InInputTag, ETriggerEvent TriggerEvent, UserObject* ContextObject, 
	CallbackFunc Func, VarTypes ...Vars)
{
	checkf(InInputConfig, TEXT("Input config data asset is null,can not proceed with binding"));

	if (UInputAction* FoundAction = InInputConfig->FindNativeInputActionByTag(InInputTag))
	{
		BindAction(FoundAction, TriggerEvent, ContextObject, Func, Vars...);
	}
}

template<class UserObject, typename CallbackFunc>
inline void URPGInputComponent::BindAbilityInputAction(const UDataAsset_InputConfig* InInputConfig, 
	UserObject* ContextObject, CallbackFunc InputPressedFunc, CallbackFunc InputReleasedFunc)
{
	checkf(InInputConfig, TEXT("Input config data asset is null,can not proceed with binding"));

	for (const FWarriorInputActionConfig& AbilityInputActionConfig : InInputConfig->AbilityInputActions)
	{
		if (!AbilityInputActionConfig.IsValid()) continue;
		
		BindAction(AbilityInputActionConfig.InputAction, ETriggerEvent::Started, ContextObject, InputPressedFunc,
				AbilityInputActionConfig.InputTag);
		BindAction(AbilityInputActionConfig.InputAction, ETriggerEvent::Completed, ContextObject, InputReleasedFunc,
				AbilityInputActionConfig.InputTag);		
	}
}
