#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGSkillCatalogSubsystem.generated.h"

class URPGSkillDefinition;
struct FStreamableHandle;

/**
 * Discovers and owns the canonical skill definitions for one game instance.
 *
 * UI and gameplay consumers depend on this catalog instead of knowing asset
 * paths or Asset Manager loading details. Definitions are loaded once and
 * retained for the lifetime of the game instance.
 */
UCLASS()
class PROJECT_RPG_API URPGSkillCatalogSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** Starts the shared async load. Concurrent callers are coalesced. */
	void RequestSkillDefinitions(FSimpleDelegate OnReady = FSimpleDelegate());

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Catalog")
	bool IsCatalogReady() const { return bCatalogReady; }

	/** Returns definitions in deterministic SkillTag order. */
	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Catalog")
	TArray<URPGSkillDefinition*> GetSkillDefinitions() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Catalog")
	URPGSkillDefinition* FindSkillDefinition(FGameplayTag SkillTag) const;

private:
	void HandleDefinitionsLoaded();
	void FinishRequest();

	UPROPERTY(Transient)
	TArray<TObjectPtr<URPGSkillDefinition>> CachedDefinitions;

	TArray<FPrimaryAssetId> PendingAssetIds;
	TArray<FSimpleDelegate> PendingCallbacks;
	TSharedPtr<FStreamableHandle> LoadHandle;
	bool bLoadInProgress = false;
	bool bCatalogReady = false;
};
