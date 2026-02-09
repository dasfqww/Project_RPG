#include "Validation/Project_RPGEditorValidator_Blueprint.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "Project_RPGEditorValidator"

UProject_RPGEditorValidator_Blueprint::UProject_RPGEditorValidator_Blueprint()
	: Super()
{
}

bool UProject_RPGEditorValidator_Blueprint::CanValidateAsset_Implementation(UObject* InAsset) const
{
	return Super::CanValidateAsset_Implementation(InAsset) && InAsset->IsA<UBlueprint>();
}

EDataValidationResult UProject_RPGEditorValidator_Blueprint::ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors)
{
	UBlueprint* BP = CastChecked<UBlueprint>(InAsset);
	
	EDataValidationResult Result = EDataValidationResult::Valid;

	// IsDirty is better checked via the Package
	if (BP->GetPackage() && BP->GetPackage()->IsDirty())
	{
		AssetFails(BP, LOCTEXT("BlueprintDirty", "Blueprint is dirty and needs to be recompiled."), ValidationErrors);
		Result = EDataValidationResult::Invalid;
	}

	if (BP->Status == BS_Error)
	{
		AssetFails(BP, LOCTEXT("BlueprintHasErrors", "Blueprint has compilation errors."), ValidationErrors);
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
