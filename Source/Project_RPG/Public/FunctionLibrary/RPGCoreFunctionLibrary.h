// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Type/RPGEnumTypes.h"
#include "Type/RPGCountDownAction.h"
#include "RPGCoreFunctionLibrary.generated.h"

class URPGGameInstance;
class APlayerController;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGCoreFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "RPG|CoreFunctionLibrary",
		meta = (Latent, WorldContext = "WorldContextObject", 
			LatentInfo = "LatentInfo", ExpandEnumAsExecs = "CountDownInput|CountDownOutput",
			TotalTime = "1.0", UpdateInterval = "0.1"))
	static void CountDown(const UObject* WorldContextObject, float TotalTime, float UpdateInterval,
		float& OutRemainingTime, ERPGCountDownActionInput CountDownInput,
		UPARAM(DisplayName = "Output") ERPGCountDownActionOutput& CountDownOutput, FLatentActionInfo LatentInfo);

	UFUNCTION(BlueprintPure, Category = "RPG|CoreFunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static URPGGameInstance* GetRPGGameInstance(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "RPG|CoreFunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static void ToggleInputMode(const UObject* WorldContextObject, ERPGInputMode InInputMode);

	UFUNCTION(BlueprintPure, Category = "RPG|CoreFunctionLibrary", meta = (DisplayName = "Format Time To MM:SS"))
	static FString FormatTimeToMMSS(float InSeconds);

	template<typename T>
	static T* GetComponentFromPlayerController(const APlayerController* PlayerController);

	template<typename TEnum>
	static bool TryConvertStringToEnum(const FString& StringKey, TEnum& OutEnum);

	template<typename TEnum>
	static FString GetEnumNameString(TEnum EnumValue);
};

template<typename T>
inline T* URPGCoreFunctionLibrary::GetComponentFromPlayerController(const APlayerController* PlayerController)
{
	if (!IsValid(PlayerController)) return nullptr;

	return PlayerController->FindComponentByClass<T>();
}

template<typename TEnum>
inline bool URPGCoreFunctionLibrary::TryConvertStringToEnum(const FString& StringKey, TEnum& OutEnum)
{
	if (const UEnum* EnumPtr = StaticEnum<TEnum>())
	{
		FName NameKey = FName(StringKey);
		int64 EnumValue = EnumPtr->GetValueByName(NameKey);
		if (EnumValue != INDEX_NONE)
		{
			OutEnum = static_cast<TEnum>(EnumValue);
			return true;
		}
	}
	return false;
}

template<typename TEnum>
inline FString URPGCoreFunctionLibrary::GetEnumNameString(TEnum EnumValue)
{
	const UEnum* EnumPtr = StaticEnum<TEnum>();
	return EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(EnumValue)) : TEXT("");
}
