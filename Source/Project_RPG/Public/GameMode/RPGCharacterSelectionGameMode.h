#pragma once

#include "GameFramework/GameModeBase.h"
#include "RPGCharacterSelectionGameMode.generated.h"

/** Hosts the local character frontend in an otherwise empty level. */
UCLASS()
class PROJECT_RPG_API ARPGCharacterSelectionGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARPGCharacterSelectionGameMode();

protected:
	virtual bool UpdatePlayerStartSpot(AController* Player, const FString& Portal, FString& OutErrorMessage) override;
};
