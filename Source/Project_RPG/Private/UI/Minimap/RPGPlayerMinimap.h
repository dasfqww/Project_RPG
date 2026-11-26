#pragma once

class APawn;
class UUserWidget;
class UWorld;

namespace RPGPlayerMinimap
{
	bool SupportsWorld(const UWorld* World);
	void Attach(UUserWidget& PlayerHUD, APawn& Pawn);
	bool ToggleFieldOverview(UUserWidget& PlayerHUD, const APawn& Pawn);
	bool ResizeFieldOverview(UUserWidget& PlayerHUD, const APawn& Pawn, float WheelDelta);
}
