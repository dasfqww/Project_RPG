// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Manager/RPGManagerContext.h"
#include "RPGGameInstanceSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGGameInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	template<typename T>
	static T* Get(UObject* WorldContextObject)
	{
		if (!WorldContextObject) return nullptr;
		UGameInstance* GameInstance = RPGManagerContext::ResolveGameInstance(WorldContextObject);
		return GameInstance ? GameInstance->GetSubsystem<T>() : nullptr;
	}
};
