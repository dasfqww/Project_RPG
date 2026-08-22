#pragma once

#include "Validation/Project_RPGEditorValidator.h"
#include "Project_RPGEditorValidator_SkillDefinition.generated.h"

/** Validates the canonical modular skill definition and its tripod structure. */
UCLASS()
class PROJECT_RPGEDITOR_API UProject_RPGEditorValidator_SkillDefinition
	: public UProject_RPGEditorValidator
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(
		UObject* InAsset, TArray<FText>& ValidationErrors) override;
};
