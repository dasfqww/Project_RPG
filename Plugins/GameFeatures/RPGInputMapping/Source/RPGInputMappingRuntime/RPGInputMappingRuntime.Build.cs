using UnrealBuildTool;

public class RPGInputMappingRuntime : ModuleRules
{
	public RPGInputMappingRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "GameFeatures", "EnhancedInput", "Project_RPG", "InputCore"
		});
	}
}
