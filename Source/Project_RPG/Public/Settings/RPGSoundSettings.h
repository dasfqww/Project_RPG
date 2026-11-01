#pragma once

#include "CoreMinimal.h"

/** Complete live audio state; persistence formats stay in the options GameFeature. */
struct PROJECT_RPG_API FRPGSoundSettings
{
    float MasterVolume = 1.f;
    float BGMVolume = 1.f;
    float EffectVolume = 1.f;
    bool bMasterMuted = false;
    bool bBGMMuted = false;
    bool bEffectMuted = false;
};

