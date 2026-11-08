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
                if (choice.ObjectiveId is { } kill && definition.KillObjectives?.ContainsKey(kill) == true)
                    throw new NarrativeConflictException("kill_objective_requires_event");
                if (choice.ObjectiveId is { } target && definition.DungeonObjectives?.ContainsKey(target) == true)
                    throw new NarrativeConflictException("dungeon_objective_requires_evidence");
                if (choice.ObjectiveId is { } interaction && definition.InteractionObjectives?.ContainsKey(interaction) == true)
                    throw new NarrativeConflictException("interaction_objective_requires_event");
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
        if (definition.DungeonObjectives is { Count: > 0 })
        {
            var activated = progress.DungeonObjectives ?? ImmutableDictionary<string, DungeonObjectiveProgress>.Empty;
            foreach (string id in definition.DungeonObjectives.Keys)
                if (!activated.ContainsKey(id) && definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains))
                    activated = activated.Add(id, new(now));
            progress = progress with { DungeonObjectives = activated };
        }
        if (definition.InteractionObjectives is { Count: > 0 })
        {
            var activated = progress.InteractionObjectives ?? ImmutableDictionary<string, InteractionObjectiveProgress>.Empty;
            foreach (string id in definition.InteractionObjectives.Keys)
                if (!activated.ContainsKey(id) && definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains))
                    activated = activated.Add(id, new(now));
            progress = progress with { InteractionObjectives = activated };
        }
        if (definition.KillObjectives is { Count: > 0 })
        {
            var activated = progress.KillObjectives ?? ImmutableDictionary<string, KillObjectiveProgress>.Empty;
            foreach (string id in definition.KillObjectives.Keys)
                if (!activated.ContainsKey(id) && definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains))
                    activated = activated.Add(id, new(now, definition.KillObjectives[id].RequiredCount));
            progress = progress with { KillObjectives = activated };
        }
        return progress;
    }

    internal NarrativeState RecordInteraction(NarrativeState state, NarrativeCatalog catalog, NarrativeInteractionCommand command,
        NarrativeInteractionContext context)
    {
        var updated = state;
        // Evaluate against the pre-event snapshot: one event cannot unlock and satisfy its next stage.
        foreach (var current in state.Quests.Where(q => q.Status == QuestStatus.Active))
        {
            var definition = catalog.Quests[current.QuestId];
            if (definition.InteractionObjectives is null || current.InteractionObjectives is null) continue;
            string[] matches = [.. definition.Objectives.Where(id =>
                definition.InteractionObjectives.TryGetValue(id, out var target)
                && target.DungeonId == context.DungeonId && target.Difficulty == context.Difficulty && target.TargetId == command.TargetId
                && CanRecordObjective(current, definition, id)
                && current.InteractionObjectives.TryGetValue(id, out var activation) && activation.ActivatedAt <= command.ObservedAtUtc)];
            if (matches.Length == 0) continue;
            var completed = current.CompletedObjectives.AddRange(matches);
            var evidence = current.InteractionObjectives;
            foreach (string id in matches) evidence = evidence.SetItem(id, evidence[id] with { EventId = command.EventId });
            var progress = current with { CompletedObjectives = completed, InteractionObjectives = evidence,
                Status = completed.Length == definition.Objectives.Length ? QuestStatus.ReadyToReport : QuestStatus.Active };
            updated = updated with { Quests = updated.Quests.Replace(current, ActivateDungeonObjectives(progress, definition, context.ReceivedAt)) };
        }
        return updated;
    }

    internal NarrativeState RecordKill(NarrativeState state, NarrativeCatalog catalog, NarrativeKillCommand command,
        NarrativeInteractionContext context)
    {
        var updated = state;
        // One death counts once for each eligible parallel goal in the PRE-event snapshot.
        foreach (var current in state.Quests.Where(q => q.Status == QuestStatus.Active))
        {
            var definition = catalog.Quests[current.QuestId];
            if (definition.KillObjectives is null || current.KillObjectives is null) continue;
            string[] matches = [.. definition.Objectives.Where(id =>
                definition.KillObjectives.TryGetValue(id, out var target) && target.DungeonId == context.DungeonId
                && target.Difficulty == context.Difficulty && target.EnemyId == command.EnemyId
                && CanRecordObjective(current, definition, id) && current.KillObjectives.TryGetValue(id, out var activation)
                && activation.ActivatedAt <= command.ObservedAtUtc)];
            if (matches.Length == 0) continue;
            var counts = current.KillObjectives; var completed = current.CompletedObjectives;
            foreach (string id in matches)
            {
                int count = checked(counts[id].Count + 1);
                counts = counts.SetItem(id, counts[id] with { Count = count, LastEventId = command.EventId });
                if (count == definition.KillObjectives[id].RequiredCount) completed = completed.Add(id);
            }
            var progress = current with { KillObjectives = counts, CompletedObjectives = completed,
                Status = completed.Length == definition.Objectives.Length ? QuestStatus.ReadyToReport : QuestStatus.Active };
            updated = updated with { Quests = updated.Quests.Replace(current, ActivateDungeonObjectives(progress, definition, context.ReceivedAt)) };
        }
        return updated;
    }
}
