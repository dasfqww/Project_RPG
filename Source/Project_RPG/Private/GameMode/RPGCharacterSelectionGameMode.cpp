#include "GameMode/RPGCharacterSelectionGameMode.h"

ARPGCharacterSelectionGameMode::ARPGCharacterSelectionGameMode()
{
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	HUDClass = nullptr;
	bStartPlayersAsSpectators = true;
}

bool ARPGCharacterSelectionGameMode::UpdatePlayerStartSpot(AController*, const FString&, FString& OutErrorMessage)
{
	OutErrorMessage.Reset();
	return true;
}
