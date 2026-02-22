// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RPGTeamFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGTeamFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	/** Returns the team this object belongs to, or 255 (NoTeam) if it is not part of a team */
	UFUNCTION(BlueprintCallable, Category = Teams, meta = (Keywords = "GetTeamFromObject", DefaultToSelf = "Agent", AdvancedDisplay = "bLogIfNotSet"))
	static void FindTeamFromObject(const UObject* Agent, bool& bIsPartOfTeam, int32& TeamId, URPGTeamDisplayAsset*& DisplayAsset, bool bLogIfNotSet = false);

	/** Returns the team display asset for the specified team ID */
	UFUNCTION(BlueprintCallable, Category = Teams, meta = (WorldContext = "WorldContextObject"))
	static URPGTeamDisplayAsset* GetTeamDisplayAsset(const UObject* WorldContextObject, int32 TeamId);

	/** Helper functions to get parameters from the display asset with a fallback if not found */
	UFUNCTION(BlueprintCallable, Category = Teams)
	static float GetTeamScalarWithFallback(URPGTeamDisplayAsset* DisplayAsset, FName ParameterName, float DefaultValue);

	UFUNCTION(BlueprintCallable, Category = Teams)
	static FLinearColor GetTeamColorWithFallback(URPGTeamDisplayAsset* DisplayAsset, FName ParameterName, FLinearColor DefaultValue);

	UFUNCTION(BlueprintCallable, Category = Teams)
	static class UTexture* GetTeamTextureWithFallback(URPGTeamDisplayAsset* DisplayAsset, FName ParameterName, class UTexture* DefaultValue);
};
