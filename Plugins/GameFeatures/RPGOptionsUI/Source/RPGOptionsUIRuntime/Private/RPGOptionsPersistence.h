#pragma once

#include "CoreMinimal.h"

struct FGraphicSaveData;
struct FSoundSaveData;

/**
 * Stateless local options I/O with staged writes and previous-file backup recovery.
 * Loads try the legacy primary filename first, then its .bak file, without repairing files during reads.
 * Saves retain a parseable previous primary before replacement; primary filenames/data structures are unchanged.
 */
namespace RPGOptionsPersistence
{
    bool LoadGraphics(FGraphicSaveData& OutData);
    bool LoadSound(FSoundSaveData& OutData);
    bool SaveGraphics(const FGraphicSaveData& Data, FText& OutError);
    bool SaveSound(const FSoundSaveData& Data, FText& OutError);
}
