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

	/** Zero = guaranteed; 1..10000 basis points in one exclusive personal pool (total <=10000). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward|PersonalLoot",
		meta = (ClampMin = "0", ClampMax = "10000"))
	int32 PersonalLootWeight = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	ERPGItemBindState BindState = ERPGItemBindState::Unbound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	FRPGItemDurability Durability;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	FGameplayTagContainer InstanceTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward")
	TArray<FRPGDungeonItemRewardStat> StatValues;

	/** Explicit opt-in. Existing rewards remain growth-unset, not implicit +0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward|Equipment")
	bool bIncludeEquipmentProgression = false;

	/** FirstSlice/rule 1; supported equipment only, quantity/stack 1, CharacterBound. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Reward|Equipment",
		meta = (ClampMin = "0", ClampMax = "3", EditCondition = "bIncludeEquipmentProgression"))
	int32 EquipmentProgressionLevel = 0;
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

	/** Guaranteed entries plus at most one personal choice; inventory first, overflow to mail. */
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
