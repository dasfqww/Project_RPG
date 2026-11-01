#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RPGExperienceSettings.generated.h"

class URPGExperienceDefinition;
class UWorld;

/** Optional Experience selection; an empty default keeps existing maps unchanged. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "RPG Experiences"))
class PROJECT_RPG_API URPGExperienceSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	TSoftObjectPtr<URPGExperienceDefinition> DefaultExperience;

	/** Key is the complete map package name, for example /Game/Maps/Lobby. */
	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	TMap<FName, TSoftObjectPtr<URPGExperienceDefinition>> MapExperiences;

	/** Keep the existing client loading screen up while a selected Experience is loading. */
	UPROPERTY(Config, EditAnywhere, Category = "Loading")
	bool bShowLoadingScreenWhileLoading = true;

	/** Passive notice after the global loading screen closes; does not change gameplay input. */
	UPROPERTY(Config, EditAnywhere, Category = "Loading")
	bool bShowLoadingFailureNotice = true;

	/** Player-facing message; raw asset paths and plugin diagnostics remain in logs. */
	UPROPERTY(Config, EditAnywhere, Category = "Loading")
	FText LoadingFailureMessage = NSLOCTEXT("RPGExperienceUI", "RestartHint", "게임을 다시 실행해 주세요.");

	UPROPERTY(Config, EditAnywhere, Category = "Loading", meta = (ClampMin = "1.0"))
	float LoadTimeoutSeconds = 60.f;

	/** Map actor selection overrides project settings. Empty paths represent an intentional opt-out. */
	FSoftObjectPath ResolveForWorld(const UWorld& World) const;

	/** Returns false on invalid/ambiguous map selection; never falls back after such an error. */
	bool TryResolveForWorld(const UWorld& World, FSoftObjectPath& OutPath, FString& OutError) const;
};
