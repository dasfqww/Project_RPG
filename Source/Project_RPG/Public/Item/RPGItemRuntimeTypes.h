#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGItemRuntimeTypes.generated.h"

class FRPGItemInstanceStateBuilder;
class FRPGItemStackPolicy;
class URPGItemDefinition;
class URPGItemInstance;
struct FRPGItemRecord;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemStatValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame)
	FGameplayTag StatTag;

	UPROPERTY(BlueprintReadOnly, SaveGame)
	float Value = 0.0f;
};

/**
 * Mutable, replicated data that belongs to one item instance.
 *
 * Static authoring data deliberately remains on URPGItemDefinition. This state
 * only contains identity, stack quantity, and values rolled at creation time.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemInstanceState
{
	GENERATED_BODY()

public:
	bool IsValid() const;
	bool HasValidIdentity() const { return InstanceId.IsValid(); }
	float GetStatValue(const FGameplayTag& StatTag, float DefaultValue = 0.0f) const;
	bool HasInstanceTag(const FGameplayTag& Tag) const;

	/**
	 * Rehydrates state from an authoritative persistence record.
	 * Quantity may be zero only for a terminal item record.
	 */
	static bool TryRestore(
		const FGuid& InInstanceId,
		int32 InGenerationSeed,
		int32 InQuantity,
		const FGameplayTagContainer& InInstanceTags,
		const TArray<FRPGItemStatValue>& InStatValues,
		FRPGItemInstanceState& OutState);

	const FGuid& GetInstanceId() const { return InstanceId; }
	int32 GetGenerationSeed() const { return GenerationSeed; }
	int32 GetQuantity() const { return Quantity; }
	const FGameplayTagContainer& GetInstanceTags() const { return InstanceTags; }
	const TArray<FRPGItemStatValue>& GetStatValues() const { return StatValues; }

private:
	void Initialize(int32 InGenerationSeed, int32 InQuantity);
	void SetQuantity(int32 InQuantity);

	friend class FRPGItemInstanceStateBuilder;
	friend class FRPGItemStackPolicy;
	friend class URPGItemDefinition;
	friend class URPGItemInstance;
	friend struct FRPGItemRecord;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	FGuid InstanceId;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	int32 GenerationSeed = 0;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	int32 Quantity = 0;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer InstanceTags;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	TArray<FRPGItemStatValue> StatValues;
};

/**
 * Narrow write surface exposed to definition fragments while an instance is
 * being built. Fragments cannot replace identity or stack quantity.
 */
class PROJECT_RPG_API FRPGItemInstanceStateBuilder
{
public:
	explicit FRPGItemInstanceStateBuilder(FRPGItemInstanceState& InState)
		: State(InState)
	{
	}

	bool AddInstanceTag(const FGameplayTag& Tag);
	bool AddStatValue(const FGameplayTag& StatTag, float Value);

private:
	FRPGItemInstanceState& State;
};
