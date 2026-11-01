#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "RPGExperienceCookReferenceValidator.generated.h"

/** Checks Experience dependencies and explicit front-end primary IDs against Packaging exclusions. */
UCLASS(Transient, NotBlueprintable)
class URPGExperienceCookReferenceValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Asset,
		FDataValidationContext& Context) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData,
		UObject* Asset, FDataValidationContext& Context) override;
};
