using UnrealBuildTool;

public class RPGInventoryRuntime : ModuleRules
{
    public RPGInventoryRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameplayTags", "NetCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "HTTP", "Json", "JsonUtilities" });
        SetupIrisSupport(Target);
    }
}
