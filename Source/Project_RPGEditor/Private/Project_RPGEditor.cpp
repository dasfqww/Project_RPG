#include "Project_RPGEditor.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Style/Project_RPGEditorStyle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "AssetToolsModule.h"
#include "IAssetTypeActions.h"
#include "Asset/AssetTypeActions_RPGSkillConfig.h"

#define LOCTEXT_NAMESPACE "Project_RPGEditor"

void FProject_RPGEditorModule::StartupModule()
{
	FProject_RPGEditorStyle::Initialize();

	// Register Asset Actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedRef<IAssetTypeActions> SkillConfigAction = MakeShareable(new FAssetTypeActions_RPGSkillConfig());
	AssetTools.RegisterAssetTypeActions(SkillConfigAction);
	RegisteredAssetTypeActions.Add(SkillConfigAction);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FProject_RPGEditorModule::RegisterMenus));
}

void FProject_RPGEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
	RegisteredAssetTypeActions.Empty();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FProject_RPGEditorStyle::Shutdown();
}

void FProject_RPGEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar"))
	{
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("ProjectRPGTools");
		
		Section.AddMenuEntry(
			"CheckContent",
			LOCTEXT("CheckContent_Label", "Check Content"),
			LOCTEXT("CheckContent_Tooltip", "Run validation on all assets in the project."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Validation"),
			FUIAction(FExecuteAction::CreateRaw(this, &FProject_RPGEditorModule::OnCheckContentClicked))
		);
	}
}

void FProject_RPGEditorModule::OnCheckContentClicked()
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FProject_RPGEditorModule, Project_RPGEditor)