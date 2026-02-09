#include "Validation/Project_RPGEditorValidator.h"

#define LOCTEXT_NAMESPACE "Project_RPGEditorValidator"

UProject_RPGEditorValidator::UProject_RPGEditorValidator()
	: Super()
{
}

bool UProject_RPGEditorValidator::CanValidateAsset_Implementation(UObject* InAsset) const
{
	return InAsset != nullptr;
}

EDataValidationResult UProject_RPGEditorValidator::ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors)
{
	return EDataValidationResult::Valid;
}

void UProject_RPGEditorValidator::AssetFails(UObject* InAsset, const FText& InMessage, TArray<FText>& ValidationErrors)
{
	ValidationErrors.Add(InMessage);
	// We don't call Super::AssetFails if it's causing issues, 
	// just adding to ValidationErrors is enough for the system to know it failed.
}

#undef LOCTEXT_NAMESPACE