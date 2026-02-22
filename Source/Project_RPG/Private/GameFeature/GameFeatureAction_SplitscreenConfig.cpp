// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeature/GameFeatureAction_SplitscreenConfig.h"
#include "Engine/Engine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureAction_SplitscreenConfig)

// Define static storage
TMap<FObjectKey, int32> UGameFeatureAction_SplitscreenConfig::GlobalDisableVotes;

void UGameFeatureAction_SplitscreenConfig::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	// Remove local votes from the global map
	for (const FObjectKey& Key : LocalDisableVotes)
	{
		if (int32* CountPtr = GlobalDisableVotes.Find(Key))
		{
			--(*CountPtr);
			if (*CountPtr <= 0)
			{
				GlobalDisableVotes.Remove(Key);
			}
		}
	}

	LocalDisableVotes.Empty();
}

void UGameFeatureAction_SplitscreenConfig::AddToWorld(const FWorldContext& WorldContext, const FGameFeatureStateChangeContext& ChangeContext)
{
	if (!bDisableSplitscreen)
	{
		return;
	}

	FObjectKey Key(this);
	if (!LocalDisableVotes.Contains(Key))
	{
		LocalDisableVotes.Add(Key);
		int32& Count = GlobalDisableVotes.FindOrAdd(Key);
		++Count;
	}
}
