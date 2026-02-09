#pragma once

#include "Validation/Project_RPGEditorValidator.h"
#include "Project_RPGEditorValidator_Blueprint.generated.h"

/**
 * 블루프린트 에셋의 유효성을 검사하는 검증기
 */
UCLASS()
class PROJECT_RPGEDITOR_API UProject_RPGEditorValidator_Blueprint : public UProject_RPGEditorValidator
{
	GENERATED_BODY()

public:
	UProject_RPGEditorValidator_Blueprint();

protected:
	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors) override;
};
