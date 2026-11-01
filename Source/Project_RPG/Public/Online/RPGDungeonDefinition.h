#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Type/RPGEnumTypes.h"
#include "RPGDungeonDefinition.generated.h"

/** Authored lobby selection data, not admission, server allocation or reward authority. */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGDungeonDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon")
	FString DungeonId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon")
	TArray<ERPGGameDifficulty> Difficulties = {ERPGGameDifficulty::Easy, ERPGGameDifficulty::Normal,
		ERPGGameDifficulty::Hard, ERPGGameDifficulty::Hell};

	/** Used only after an explicit dungeon change when the old difficulty is unsupported. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon")
	ERPGGameDifficulty DefaultDifficulty = ERPGGameDifficulty::Hard;

	static bool IsValidDungeonId(const FString& Id);
	bool ValidateSelection(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
