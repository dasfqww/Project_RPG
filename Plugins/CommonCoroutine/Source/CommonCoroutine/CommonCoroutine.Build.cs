using UnrealBuildTool;

public class CommonCoroutine : ModuleRules
{
	public CommonCoroutine(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);
		
		PublicDependencyModuleNames.AddRange(["Core", "CoreUObject", "Engine", "GameFeatures", "GameplayAbilities", "GameplayTags"]);
	}
}