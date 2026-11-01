#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Type/RPGEnumTypes.h"
#include "RPGDungeonServerDefinition.generated.h"

class URPGDungeonRewardDefinition;
class URPGDungeonServerDefinition;
class UWorld;

/** Pure resolved data. The owning authoritative GameMode pins a copy before admission. */
struct PROJECT_RPG_API FRPGDungeonServerSelection
{
	const URPGDungeonServerDefinition* Definition = nullptr;
	const URPGDungeonRewardDefinition* RewardDefinition = nullptr;
	FString RewardVersion;
	TArray<FRPGCurrencyChange> CurrencyChanges;
	TArray<FRPGDungeonItemReward> ItemRewards;
};

/** Trusted server-authored mapping. Never supplied by a player's session/create request. */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGDungeonServerDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon")
	FString DungeonId;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon")
	TSoftObjectPtr<UWorld> DungeonMap;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon")
	TMap<ERPGGameDifficulty, TObjectPtr<URPGDungeonRewardDefinition>> ClearRewardsByDifficulty;
	/** Metadata only: does not load/open the map while authoring or resolving an assignment. */
	UFUNCTION(BlueprintPure, Category = "Dungeon")
	FString GetDungeonMapPackage() const { return DungeonMap.ToSoftObjectPath().GetLongPackageName(); }

	bool ValidateDefinition(FString& OutError) const;
	static bool ValidateCatalog(const TArray<TObjectPtr<URPGDungeonServerDefinition>>& Definitions, FString& OutError);
	static bool BuildAssignedSelection(const TArray<TObjectPtr<URPGDungeonServerDefinition>>& Definitions,
		const FString& DungeonId, const FString& ActualMapPackage, ERPGGameDifficulty Difficulty,
		FRPGDungeonServerSelection& OutSelection, FString& OutError);
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
