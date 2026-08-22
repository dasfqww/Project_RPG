#pragma once

#include "CoreMinimal.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Engine/DataAsset.h"
#include "RPGDungeonRewardDefinition.generated.h"

class FDataValidationContext;
class URPGItemDefinition;

/**
 * Authored item reward. The backend identity and definition version are read
 * from ItemDefinition so content authors do not enter persistence IDs by hand.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonItemRewardEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	TObjectPtr<const URPGItemDefinition> ItemDefinition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward",
		meta = (ClampMin = "1"))
	int32 Quantity = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	ERPGItemBindState BindState = ERPGItemBindState::Unbound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	FRPGItemDurability Durability;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	FGameplayTagContainer InstanceTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	TArray<FRPGDungeonItemRewardStat> StatValues;
};

/**
 * One versioned, deterministic reward bundle for a dungeon difficulty.
 * A dedicated server converts this asset into the persistence-only transport
 * structs before requesting the backend's atomic party settlement.
 */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGDungeonRewardDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** ASCII identifier used as part of the backend idempotency command. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	FName RewardVersion;

	/** Every change is granted to every character in the dungeon snapshot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	TArray<FRPGCurrencyChange> CurrencyChanges;

	/** Every item is delivered to every character's dungeon reward mail. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward",
		meta = (TitleProperty = "ItemDefinition"))
	TArray<FRPGDungeonItemRewardEntry> ItemRewards;

	/** Validates and creates a backend settlement payload without mutation. */
	bool BuildSettlement(
		FString& OutRewardVersion,
		TArray<FRPGCurrencyChange>& OutCurrencyChanges,
		TArray<FRPGDungeonItemReward>& OutItemRewards,
		FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(
		FDataValidationContext& Context) const override;
#endif
};
