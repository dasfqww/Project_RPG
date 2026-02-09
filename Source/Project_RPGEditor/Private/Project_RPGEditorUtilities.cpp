#include "Project_RPGEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorValidatorSubsystem.h"
#include "Editor.h"

void UProject_RPGEditorUtilities::CompileAllBlueprintsInPath(FString Path)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByPath(*Path, AssetDataList, true);

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset()))
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
		}
	}
}

void UProject_RPGEditorUtilities::RunAllValidations()
{
	if (!GEditor) return;

	TArray<FAssetData> AllAssets;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().GetAssetsByPath(TEXT("/Game"), AllAssets, true);

	if (AllAssets.Num() > 0)
	{
		UEditorValidatorSubsystem* ValidationSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
		if (ValidationSubsystem)
		{
			FValidateAssetsSettings Settings;
			FValidateAssetsResults Results;
			ValidationSubsystem->ValidateAssetsWithSettings(AllAssets, Settings, Results);
		}
	}
}