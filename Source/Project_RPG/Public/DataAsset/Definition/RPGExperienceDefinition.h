// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RPGExperienceDefinition.generated.h"

class UGameFeatureAction;
class URPGExperienceActionSet;

/**
 * Data that describes the game features and actions required by an experience.
 */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGExperienceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	URPGExperienceDefinition();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

#if WITH_EDITORONLY_DATA
	virtual void UpdateAssetBundleData() override;
#endif

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
	TArray<FString> GameFeaturesToEnable;

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
	TObjectPtr<const UPrimaryDataAsset> DefaultPawnData;

	UPROPERTY(EditDefaultsOnly, Instanced, Category = "Actions")
	TArray<TObjectPtr<UGameFeatureAction>> Actions;

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
	TArray<TObjectPtr<URPGExperienceActionSet>> ActionSets;
};
