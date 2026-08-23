// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Project_RPG : ModuleRules
{

	public Project_RPG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreOnline", "NetCore", "StructUtils",
            "CoreUObject", "Engine", "InputCore",
            "Slate", "SlateCore","GameplayTags", "EnhancedInput", "GameplayAbilities",
            "UMG", "GameplayTasks", "AnimGraphRuntime",
            "MotionWarping", "Niagara", "NavigationSystem", "AIModule",
            "AnimationCore", "HTTP", "Json", "JsonUtilities", "MoviePlayer", "AssetRegistry",
            "IrisCore", "ModelViewViewModel",
            "OnlineSubsystem", "OnlineSubsystemUtils",
            "CommonUI", "CommonInput", "CommonCoroutine", "CommonUser","UIExtension", "GameFeatures", "CommonGame" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Registers this module's native FastArray serializers with Iris.
		// Without this, replicated RPG FastArray properties are rejected when
		// the GameNetDriver builds its Iris replication descriptors.
		SetupIrisSupport(Target);


        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
