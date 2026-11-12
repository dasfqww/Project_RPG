#include "Narrative/RPGNarrativePresentation.h"
#include "Narrative/RPGNarrativeTypes.h"

FText FRPGNarrativePresentation::QuestHeading(const FRPGQuestProgress& Quest)
{
    const FText Title = Quest.Title.IsEmpty() ? NSLOCTEXT("RPGNarrative", "UnnamedQuest", "의뢰") : Quest.Title;
    const FText State = Quest.Status == ERPGQuestStatus::Completed ? NSLOCTEXT("RPGNarrative", "CompletedQuest", "완료")
        : Quest.Status == ERPGQuestStatus::ReadyToReport ? NSLOCTEXT("RPGNarrative", "ReportableQuest", "보고 가능")
        : NSLOCTEXT("RPGNarrative", "ActiveQuest", "진행 중");
    if (Quest.Category == ERPGQuestCategory::Unspecified)
        return FText::Format(NSLOCTEXT("RPGNarrative", "JournalHeading", "{0} · {1}"), Title, State);
    const FText Kind = Quest.Category == ERPGQuestCategory::Main
        ? NSLOCTEXT("RPGNarrative", "MainQuest", "메인 의뢰") : NSLOCTEXT("RPGNarrative", "SideQuest", "선택 서브 의뢰");
    return FText::Format(NSLOCTEXT("RPGNarrative", "CategorizedHeading", "[{0}] {1} · {2}"), Kind, Title, State);
}

FText FRPGNarrativePresentation::ObjectiveLine(const FRPGQuestObjectiveView& Objective, const int32 Index)
{
    const FText Label = Objective.Text.IsEmpty() ? FText::Format(NSLOCTEXT("RPGNarrative", "UnnamedObjective", "목표 {0}"), FText::AsNumber(Index + 1)) : Objective.Text;
    const FText Marker = Objective.bCompleted ? NSLOCTEXT("RPGNarrative", "GoalDone", "[완료]") : FText::FromString(TEXT("[ ]"));
    if (Objective.bHasCount)
        return FText::Format(NSLOCTEXT("RPGNarrative", "JournalGoalCount", "  {0} {1} ({2}/{3})"),
            Marker, Label, FText::AsNumber(Objective.CurrentCount), FText::AsNumber(Objective.RequiredCount));
    return FText::Format(NSLOCTEXT("RPGNarrative", "JournalGoal", "  {0} {1}"), Marker, Label);
}
