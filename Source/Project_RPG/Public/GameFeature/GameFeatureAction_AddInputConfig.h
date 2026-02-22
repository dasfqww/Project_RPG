// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFeature/UGFAction_WorldActionBase.h"
#include "GameFeatureAction_AddInputConfig.generated.h"

/**
 * GameFeatureAction responsible for adding input config.
 * Currently a stub as Project_RPG uses GameFeatureAction_AddInputBinding for UDataAsset_InputConfig.
 */
UCLASS(meta = (DisplayName = "Add Input Config"))
class PROJECT_RPG_API UGameFeatureAction_AddInputConfig : public UGameFeatureAction_WorldActionBase
{
	GENERATED_BODY()
	
public:
	//~ Begin UGameFeatureAction interface
	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override {}
	//~ End UGameFeatureAction interface

	//~ Begin UGameFeatureAction_WorldActionBase interface
	virtual void AddToWorld(const FWorldContext& WorldContext, const FGameFeatureStateChangeContext& ChangeContext) override {}
	//~ End UGameFeatureAction_WorldActionBase interface
};
