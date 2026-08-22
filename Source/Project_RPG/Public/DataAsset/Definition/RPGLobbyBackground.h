#pragma once

#include "Engine/DataAsset.h"
#include "RPGLobbyBackground.generated.h"

class UWorld;

/** Lobby background level data used by user-facing experiences. */
UCLASS(Config = EditorPerProjectUserSettings, BlueprintType, Const)
class PROJECT_RPG_API URPGLobbyBackground : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby")
	TSoftObjectPtr<UWorld> BackgroundLevel;
};
