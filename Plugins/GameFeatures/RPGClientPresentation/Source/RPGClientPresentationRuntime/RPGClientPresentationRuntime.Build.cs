using UnrealBuildTool;

public class RPGClientPresentationRuntime : ModuleRules
{
	public RPGClientPresentationRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameFeatures" });
		PrivateDependencyModuleNames.Add("Project_RPG");
	}
}
