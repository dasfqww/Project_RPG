#pragma once

#include "CoreMinimal.h"

namespace RPGOnlineLobbyDevelopmentProfile
{

struct FSelection
{
	bool bTraining = false;
	FString Error;
	bool bIntro1 = false;
	bool bIntro2 = false;
	FString EntryDungeonId(int32 Milestones) const
	{
		return bTraining && Error.IsEmpty() ? (bIntro2 && (Milestones & 2) != 0 ? TEXT("Progression.Intro2") : bIntro1 && (Milestones & 1) != 0
			? TEXT("Progression.Intro1") : TEXT("Progression.Training")) : FString();
	}
};

// Pure opt-in parsing. The caller supplies the existing non-Shipping development
// UI gate; this does not authenticate a player or authorize dungeon admission.
FSelection Resolve(const TCHAR* CommandLine, bool bDevelopmentUIEnabled);

}
