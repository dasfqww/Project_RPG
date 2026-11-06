using System.Collections.Immutable;
using ProjectRpg.Backend.Data;

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

    public NarrativeState Apply(NarrativeState state, QuestDefinition definition, DialogueChoice choice,
        DateTimeOffset? now = null)
    {
        QuestProgress? current = state.Quests.SingleOrDefault(q => q.QuestId == definition.Id);
        QuestProgress updated;
        switch (choice.Effect)
        {
            case DialogueEffect.AcceptQuest:
                if (!CanAccept(state, definition)) throw new NarrativeConflictException("quest_not_available");
                updated = new(definition.Id, definition.Version, QuestStatus.Active, []);
                return state with { Quests = state.Quests.Add(ActivateDungeonObjectives(updated, definition, now ?? DateTimeOffset.UtcNow)) };
            case DialogueEffect.RecordObjective:
                if (choice.ObjectiveId is { } target && definition.DungeonObjectives?.ContainsKey(target) == true)
                    throw new NarrativeConflictException("dungeon_objective_requires_evidence");
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
        return state with { Quests = state.Quests.Replace(current!,
            ActivateDungeonObjectives(updated, definition, now ?? DateTimeOffset.UtcNow)) };
    }

    internal NarrativeState RecordDungeonObjective(NarrativeState state, QuestDefinition definition,
        string objectiveId, NarrativeDungeonClearEvidence evidence, DateTimeOffset now)
    {
        QuestProgress? current = state.Quests.SingleOrDefault(q => q.QuestId == definition.Id);
        if (definition.DungeonObjectives?.GetValueOrDefault(objectiveId) is not { } requirement
            || !CanRecordObjective(current, definition, objectiveId)
            || current!.DungeonObjectives?.GetValueOrDefault(objectiveId) is not { } activated)
            throw new NarrativeConflictException("dungeon_objective_not_available");
        if (evidence.DungeonId != requirement.DungeonId || evidence.Difficulty != requirement.Difficulty)
            throw new NarrativeConflictException("dungeon_objective_target_mismatch");
        var objectiveProgress = current!.DungeonObjectives!;
        if (objectiveProgress.Values.Any(p => p.DungeonSessionId == evidence.DungeonSessionId))
            throw new NarrativeConflictException("dungeon_objective_evidence_already_used");
        if (evidence.OccurredAt < activated.ActivatedAt || evidence.OccurredAt > now)
            throw new NarrativeConflictException("dungeon_objective_event_out_of_window");
        ImmutableArray<string> objectives = current.CompletedObjectives.Add(objectiveId);
        QuestProgress updated = current with
        {
            CompletedObjectives = objectives,
            Status = objectives.Length == definition.Objectives.Length ? QuestStatus.ReadyToReport : QuestStatus.Active,
            DungeonObjectives = objectiveProgress.SetItem(objectiveId,
                activated with { DungeonSessionId = evidence.DungeonSessionId })
        };
        return state with { Quests = state.Quests.Replace(current, ActivateDungeonObjectives(updated, definition, now)) };
    }

    private static QuestProgress ActivateDungeonObjectives(QuestProgress progress, QuestDefinition definition, DateTimeOffset now)
    {
        if (definition.DungeonObjectives is null || definition.DungeonObjectives.Count == 0) return progress;
        var activated = progress.DungeonObjectives ?? ImmutableDictionary<string, DungeonObjectiveProgress>.Empty;
        foreach (string id in definition.DungeonObjectives.Keys)
            if (!activated.ContainsKey(id) && definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains))
                activated = activated.Add(id, new(now));
        return progress with { DungeonObjectives = activated };
    }
}
