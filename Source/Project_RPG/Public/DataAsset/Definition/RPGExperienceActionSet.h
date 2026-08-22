#pragma once

#include "Engine/DataAsset.h"
#include "RPGExperienceActionSet.generated.h"

class UGameFeatureAction;

/** Reusable group of actions and game-feature dependencies for an experience. */
UCLASS(BlueprintType, NotBlueprintable)
class PROJECT_RPG_API URPGExperienceActionSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	URPGExperienceActionSet();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

#if WITH_EDITORONLY_DATA
	virtual void UpdateAssetBundleData() override;
#endif

	UPROPERTY(EditAnywhere, Instanced, Category = "Actions to Perform")
	TArray<TObjectPtr<UGameFeatureAction>> Actions;

	UPROPERTY(EditAnywhere, Category = "Feature Dependencies")
	TArray<FString> GameFeaturesToEnable;
};
