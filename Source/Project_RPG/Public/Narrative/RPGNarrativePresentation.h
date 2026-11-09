#pragma once
#include "CoreMinimal.h"

struct FRPGQuestObjectiveView;

/** Shared presentation for the journal's already validated server read model. */
struct PROJECT_RPG_API FRPGNarrativePresentation
{
    static FText ObjectiveLine(const FRPGQuestObjectiveView& Objective, int32 Index);
};
