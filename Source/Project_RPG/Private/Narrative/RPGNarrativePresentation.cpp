#include "Narrative/RPGNarrativePresentation.h"
#include "Narrative/RPGNarrativeTypes.h"

FText FRPGNarrativePresentation::ObjectiveLine(const FRPGQuestObjectiveView& Objective, const int32 Index)
{
    const FText Label = Objective.Text.IsEmpty() ? FText::Format(NSLOCTEXT("RPGNarrative", "UnnamedObjective", "목표 {0}"), FText::AsNumber(Index + 1)) : Objective.Text;
    const FText Marker = Objective.bCompleted ? NSLOCTEXT("RPGNarrative", "GoalDone", "[완료]") : FText::FromString(TEXT("[ ]"));
    if (Objective.bHasCount)
        return FText::Format(NSLOCTEXT("RPGNarrative", "JournalGoalCount", "  {0} {1} ({2}/{3})"),
            Marker, Label, FText::AsNumber(Objective.CurrentCount), FText::AsNumber(Objective.RequiredCount));
    return FText::Format(NSLOCTEXT("RPGNarrative", "JournalGoal", "  {0} {1}"), Marker, Label);
}
