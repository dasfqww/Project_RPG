#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "RPGDungeonRewardTypes.generated.h"

class FJsonObject;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonItemRewardStat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FGameplayTag StatTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	double Value = 0.0;
};

/** Server-authored grant: inventory first, overflow to reward mail. */
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

	/** Definition-derived limit. Zero preserves mail-only legacy payloads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward",
		meta = (ClampMin = "0"))
	int32 MaxStackSize = 0;

	/** Zero = guaranteed/omitted on wire. Positive basis points enter one backend-only exclusive pool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward|PersonalLoot",
		meta = (ClampMin = "0", ClampMax = "10000"))
	int32 PersonalLootWeight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	ERPGItemBindState BindState = ERPGItemBindState::Unbound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FRPGItemDurability Durability;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	FGameplayTagContainer InstanceTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Reward")
	TArray<FRPGDungeonItemRewardStat> StatValues;

	/** Unset preserves legacy rewards; never infer +0 from the definition. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Reward")
	FRPGEquipmentProgressionState EquipmentProgression;

	/** The same exact definition/stack/binding restrictions as the backend. */
	bool HasValidEquipmentProgression() const;

	/** Adds the optional wire field, omitting unset state. Invalid input does not mutate Object. */
	bool WriteEquipmentProgression(FJsonObject& Object) const;

	/** Positive weights need inventory-first stack limits; zero preserves legacy payloads. */
	bool HasValidPersonalLootWeight() const;
	bool WritePersonalLootWeight(FJsonObject& Object) const;
	static bool HasValidPersonalLootPool(const TArray<FRPGDungeonItemReward>& Rewards);
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonRewardDeliveryNotice
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Reward")
	FString DungeonSessionId;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Reward")
	FString RewardVersion;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Reward")
	int32 InventoryQuantity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Reward")
	int32 MailQuantity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Reward")
	bool bApplied = false;
};
