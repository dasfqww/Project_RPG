#pragma once

#include "CoreMinimal.h"
#include "Type/RPGEnumTypes.h"

namespace RPGDungeonRewardPreview
{
	FText DescribeClearRewards(const FString& DungeonId, ERPGGameDifficulty Difficulty);
	FText DescribeFirstBossMaterial(FName Material, const FText& DungeonName);
}
