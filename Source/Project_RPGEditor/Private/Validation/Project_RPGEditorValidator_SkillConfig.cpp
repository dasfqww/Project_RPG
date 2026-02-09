#include "Validation/Project_RPGEditorValidator_SkillConfig.h"
#include "DataAsset/RPGSkillConfig.h"
#include "Ability/RPGGameplayAbility.h"

#define LOCTEXT_NAMESPACE "Project_RPGEditorValidator"

UProject_RPGEditorValidator_SkillConfig::UProject_RPGEditorValidator_SkillConfig()
	: Super()
{
}

bool UProject_RPGEditorValidator_SkillConfig::CanValidateAsset_Implementation(UObject* InAsset) const
{
	return Super::CanValidateAsset_Implementation(InAsset) && InAsset->IsA<URPGSkillConfig>();
}

EDataValidationResult UProject_RPGEditorValidator_SkillConfig::ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors)
{
	URPGSkillConfig* SkillConfig = CastChecked<URPGSkillConfig>(InAsset);

	EDataValidationResult Result = EDataValidationResult::Valid;

	if (SkillConfig->SkillName.IsEmpty())
	{
		AssetFails(SkillConfig, LOCTEXT("SkillNameEmpty", "Skill Name is empty."), ValidationErrors);
		Result = EDataValidationResult::Invalid;
	}

	if (SkillConfig->SkillIcon.IsNull())
	{
		AssetFails(SkillConfig, LOCTEXT("SkillIconNull", "Skill Icon is not assigned."), ValidationErrors);
		Result = EDataValidationResult::Invalid;
	}

	if (!SkillConfig->AbilityClass)
	{
		AssetFails(SkillConfig, LOCTEXT("AbilityClassNull", "Ability Class is not assigned."), ValidationErrors);
		Result = EDataValidationResult::Invalid;
	}

	if (SkillConfig->BaseCooldownTime < 0.0f)
	{
		AssetFails(SkillConfig, LOCTEXT("CooldownNegative", "Base Cooldown Time cannot be negative."), ValidationErrors);
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE