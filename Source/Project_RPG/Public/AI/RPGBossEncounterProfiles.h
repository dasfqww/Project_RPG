#pragma once

#include "Engine/DataAsset.h"
#include "Type/RPGEnumTypes.h"
#include "RPGBossEncounterProfiles.generated.h"

class URPGBossDefinition;

/** Explicit complete encounter definitions per difficulty. No recursive override/inheritance rules. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGBossEncounterProfiles : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	TMap<ERPGGameDifficulty, TObjectPtr<URPGBossDefinition>> Profiles;
	/** Optional explicit fallback. An unmapped difficulty otherwise rejects encounter start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss") TObjectPtr<URPGBossDefinition> Fallback;

	const URPGBossDefinition* Resolve(ERPGGameDifficulty Difficulty) const;
	bool ValidateProfiles(FText& OutError) const;
	/** Adapter to the authoritative GameMode; online assignments must already be confirmed. */
	const URPGBossDefinition* ResolveForWorld(const UWorld* World, FText& OutError) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
