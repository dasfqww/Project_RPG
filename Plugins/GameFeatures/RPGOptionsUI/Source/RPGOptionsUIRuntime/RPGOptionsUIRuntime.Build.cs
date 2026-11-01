using UnrealBuildTool;

public class RPGOptionsUIRuntime : ModuleRules
{
	public RPGOptionsUIRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "UMG", "GameFeatures", "Project_RPG", "EnhancedInput", "InputCore"
		});
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "CommonUI", "Json", "JsonUtilities", "GameplayTags" });
	}
}
