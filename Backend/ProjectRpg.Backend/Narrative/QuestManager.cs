using System.Collections.Immutable;

namespace ProjectRpg.Backend.Narrative;

/// <summary>Quest rules only. No transport, UI, chapter or reward storage.</summary>
public sealed class QuestManager
{
    public bool CanAccept(NarrativeState state, QuestDefinition definition) =>
        !state.Quests.Any(q => q.QuestId == definition.Id)
        && definition.Prerequisites.All(id => state.Quests.Any(q => q.QuestId == id && q.Status == QuestStatus.Completed));

    public bool CanRecordObjective(QuestProgress? progress, QuestDefinition definition, string objectiveId) =>
        progress is { Status: QuestStatus.Active } && progress.QuestId == definition.Id
        && progress.DefinitionVersion == definition.Version && definition.Objectives.Contains(objectiveId)
        && !progress.CompletedObjectives.Contains(objectiveId)
        && definition.RequiredObjectives(objectiveId).All(progress.CompletedObjectives.Contains);

    public NarrativeState Apply(NarrativeState state, QuestDefinition definition, DialogueChoice choice)
    {
        QuestProgress? current = state.Quests.SingleOrDefault(q => q.QuestId == definition.Id);
        QuestProgress updated;
        switch (choice.Effect)
        {
            case DialogueEffect.AcceptQuest:
                if (!CanAccept(state, definition)) throw new NarrativeConflictException("quest_not_available");
                updated = new(definition.Id, definition.Version, QuestStatus.Active, []);
                return state with { Quests = state.Quests.Add(updated) };
            case DialogueEffect.RecordObjective:
                if (current is null || current.Status != QuestStatus.Active || choice.ObjectiveId is null
                    || !definition.Objectives.Contains(choice.ObjectiveId))
                    throw new NarrativeConflictException("objective_not_available");
                if (current.CompletedObjectives.Contains(choice.ObjectiveId))
                    throw new NarrativeConflictException("objective_already_recorded");
                if (!CanRecordObjective(current, definition, choice.ObjectiveId))
                    throw new NarrativeConflictException("objective_prerequisite_not_met");
                ImmutableArray<string> objectives = current.CompletedObjectives.Add(choice.ObjectiveId);
                updated = current with { CompletedObjectives = objectives,
                    Status = objectives.Length == definition.Objectives.Length ? QuestStatus.ReadyToReport : QuestStatus.Active };
                break;
            case DialogueEffect.ReportQuest:
                if (current is null || current.Status != QuestStatus.ReadyToReport)
                    throw new NarrativeConflictException("quest_not_ready");
                updated = current with { Status = QuestStatus.Completed };
                break;
            default: throw new NarrativeConflictException("unsupported_dialogue_effect");
        }
        return state with { Quests = state.Quests.Replace(current!, updated) };
    }
}
