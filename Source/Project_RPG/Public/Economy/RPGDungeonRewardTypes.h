#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "RPGDungeonRewardTypes.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonItemRewardStat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FGameplayTag StatTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	double Value = 0.0;
};

/** Backend-authored item grant delivered to each party member's reward mail. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonItemReward
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FName DefinitionType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FName DefinitionName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward",
		meta = (ClampMin = "1"))
	int32 DefinitionVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward",
		meta = (ClampMin = "1"))
	int32 Quantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	ERPGItemBindState BindState = ERPGItemBindState::Unbound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FRPGItemDurability Durability;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FGameplayTagContainer InstanceTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	TArray<FRPGDungeonItemRewardStat> StatValues;
};
