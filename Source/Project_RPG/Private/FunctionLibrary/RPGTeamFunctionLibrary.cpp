// Fill out your copyright notice in the Description page of Project Settings.


#include "FunctionLibrary/RPGTeamFunctionLibrary.h"
#include "Manager/TeamManager.h"
#include "DataAsset/Team/RPGTeamDisplayAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Type/RPGEnumTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGTeamFunctionLibrary)

void URPGTeamFunctionLibrary::FindTeamFromObject(const UObject* Agent, bool& bIsPartOfTeam, int32& TeamId, URPGTeamDisplayAsset*& DisplayAsset, bool bLogIfNotSet)
{
	bIsPartOfTeam = false;
	TeamId = 255; // ERPGTeamID::NoTeam
	DisplayAsset = nullptr;

	if (UWorld* World = GEngine->GetWorldFromContextObject(Agent, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (UTeamManager* TeamManager = World->GetSubsystem<UTeamManager>())
		{
			TeamId = TeamManager->FindTeamFromObject(Agent);
			if (TeamId != 255)
			{
				bIsPartOfTeam = true;
				DisplayAsset = TeamManager->GetTeamDisplayAsset(static_cast<ERPGTeamID>(TeamId));

				if (DisplayAsset == nullptr && bLogIfNotSet)
				{
					UE_LOG(LogTemp, Log, TEXT("FindTeamFromObject(%s) called too early (found team %d but no display asset set yet)"), *GetPathNameSafe(Agent), TeamId);
				}
			}
		}
		else if (bLogIfNotSet)
		{
			UE_LOG(LogTemp, Error, TEXT("FindTeamFromObject(%s) failed: Team manager does not exist yet"), *GetPathNameSafe(Agent));
		}
	}
}

URPGTeamDisplayAsset* URPGTeamFunctionLibrary::GetTeamDisplayAsset(const UObject* WorldContextObject, int32 TeamId)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (UTeamManager* TeamManager = World->GetSubsystem<UTeamManager>())
		{
			return TeamManager->GetTeamDisplayAsset(static_cast<ERPGTeamID>(TeamId));
		}
	}
	return nullptr;
}

float URPGTeamFunctionLibrary::GetTeamScalarWithFallback(URPGTeamDisplayAsset* DisplayAsset, FName ParameterName, float DefaultValue)
{
	if (DisplayAsset)
	{
		if (float* pValue = DisplayAsset->ScalarParameters.Find(ParameterName))
		{
			return *pValue;
		}
	}
	return DefaultValue;
}

FLinearColor URPGTeamFunctionLibrary::GetTeamColorWithFallback(URPGTeamDisplayAsset* DisplayAsset, FName ParameterName, FLinearColor DefaultValue)
{
	if (DisplayAsset)
	{
		if (FLinearColor* pColor = DisplayAsset->ColorParameters.Find(ParameterName))
		{
			return *pColor;
		}

		// Hardcoded fallbacks for built-in properties
		if (ParameterName == TEXT("TeamColor")) return DisplayAsset->TeamColor;
		if (ParameterName == TEXT("OutlineColor")) return DisplayAsset->OutlineColor;
	}
	return DefaultValue;
}

UTexture* URPGTeamFunctionLibrary::GetTeamTextureWithFallback(URPGTeamDisplayAsset* DisplayAsset, FName ParameterName, UTexture* DefaultValue)
{
	if (DisplayAsset)
	{
		if (TObjectPtr<UTexture>* pTexture = DisplayAsset->TextureParameters.Find(ParameterName))
		{
			return *pTexture;
		}

		// Hardcoded fallbacks for built-in properties
		if (ParameterName == TEXT("TeamIcon")) return Cast<UTexture>(DisplayAsset->TeamIcon);
		if (ParameterName == TEXT("DefaultRaidMarker")) return Cast<UTexture>(DisplayAsset->DefaultRaidMarker);
	}
	return DefaultValue;
}

