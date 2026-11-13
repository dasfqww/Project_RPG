#pragma once

#include "CoreMinimal.h"

/** Visual settings only. Slot contents, input and action availability belong to the caller. */
struct FRPGSlotAppearance
{
    FLinearColor HighlightColor = FLinearColor(1.0f, 0.025f, 0.012f, 0.85f);
    float HighlightWidth = 6.0f;
    float FadeInDuration = 0.12f;
};
