#pragma once
#include "CoreMinimal.h"

struct FRPGQuestObjectiveView;
struct FRPGQuestProgress;

/** Shared presentation for the journal's already validated server read model. */
struct PROJECT_RPG_API FRPGNarrativePresentation
{
    static FText QuestHeading(const FRPGQuestProgress& Quest);
    static FText ObjectiveLine(const FRPGQuestObjectiveView& Objective, int32 Index);
};
