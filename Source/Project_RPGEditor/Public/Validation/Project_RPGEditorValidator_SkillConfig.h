#pragma once

#include "Validation/Project_RPGEditorValidator.h"
#include "Project_RPGEditorValidator_SkillConfig.generated.h"

/**
 * URPGSkillConfig 데이터 에셋의 유효성을 검사하는 검증기
 */
UCLASS()
class PROJECT_RPGEDITOR_API UProject_RPGEditorValidator_SkillConfig : public UProject_RPGEditorValidator
{
	GENERATED_BODY()

public:
	UProject_RPGEditorValidator_SkillConfig();

protected:
	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors) override;
};
