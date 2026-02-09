using UnrealBuildTool;

public class Project_RPGEditor : ModuleRules
{
	public Project_RPGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"Project_RPG" // 런타임 모듈 참조
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Slate",
				"SlateCore",
				"ToolMenus",
				"AssetTools",
				"EditorWidgets",
				"KismetWidgets",
				"PropertyEditor",
				"ContentBrowser",
				"EditorSubsystem",
				"Blutility", // Editor Utility Widgets를 위해 필요
				"UMG",
				"UMGEditor",
				"DataValidation"
			}
		);
	}
}
