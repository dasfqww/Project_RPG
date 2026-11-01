using UnrealBuildTool;

public class RPGClientUIRuntime : ModuleRules
{
	public RPGClientUIRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "UMG", "GameFeatures"
		});
	}
}
