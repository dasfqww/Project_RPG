#pragma once

#include "CoreMinimal.h"

namespace RPGOnlineLobbyDevelopmentProfile
{

struct FSelection
{
	bool bTraining = false;
	FString Error;
};

// Pure opt-in parsing. The caller supplies the existing non-Shipping development
// UI gate; this does not authenticate a player or authorize dungeon admission.
FSelection Resolve(const TCHAR* CommandLine, bool bDevelopmentUIEnabled);

}
