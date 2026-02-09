#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "Project_RPGEditorValidator.generated.h"

/**
 * Base class for project asset validation.
 */
UCLASS(Abstract)
class PROJECT_RPGEDITOR_API UProject_RPGEditorValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	UProject_RPGEditorValidator();

protected:
	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors) override;

	void AssetFails(UObject* InAsset, const FText& InMessage, TArray<FText>& ValidationErrors);
};
