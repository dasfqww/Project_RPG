#pragma once

class APawn;
class UUserWidget;
class UWorld;

namespace RPGPlayerMinimap
{
	bool SupportsWorld(const UWorld* World);
	void Attach(UUserWidget& PlayerHUD, APawn& Pawn);
	void ToggleFieldOverview(UUserWidget& PlayerHUD, const APawn& Pawn);
}
