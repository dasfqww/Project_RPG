#pragma once

#include "CoreMinimal.h"
#include "RPGActionBarActionView.generated.h"

/** Read-only feature projection. Reasons are localized display text, never server diagnostics. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGActionBarActionView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
    UPROPERTY(BlueprintReadOnly) FText KeyLabel;
    UPROPERTY(BlueprintReadOnly) float CooldownRemaining = 0.f;
    UPROPERTY(BlueprintReadOnly) float CooldownDuration = 0.f;
    /** Displayed only while unavailable. Empty selects the common fallback message. */
    UPROPERTY(BlueprintReadOnly) FText UnavailableReason;
};
