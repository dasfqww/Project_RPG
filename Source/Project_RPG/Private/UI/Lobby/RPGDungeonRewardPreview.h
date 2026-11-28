#pragma once

#include "CoreMinimal.h"
#include "Type/RPGEnumTypes.h"

class URPGDungeonRewardDefinition;

namespace RPGDungeonRewardPreview
{
	const URPGDungeonRewardDefinition* FindReward(const FString& DungeonId, ERPGGameDifficulty Difficulty);
	FText DescribeClearRewards(const FString& DungeonId, ERPGGameDifficulty Difficulty);
	FText DescribeFirstBossMaterial(FName Material, const FText& DungeonName);
}
