using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Application;

namespace ProjectRpg.Backend.Narrative;

public sealed record DungeonObjectiveCommand(Guid RequestId, long ExpectedRevision, int CatalogVersion,
    Guid DungeonSessionId, string QuestId, string ObjectiveId);

/// <summary>Records one authored goal from durable clear evidence. No reward or client completion flags.</summary>
public sealed class DungeonObjectiveCoordinator(INarrativeRepository repository,
    INarrativeDungeonEvidenceRepository evidenceRepository, NarrativeCatalog catalog, QuestManager quests,
    ScenarioManager scenarios, NarrativeCoordinator narrative, TimeProvider clock)
{
    internal static IReadOnlyList<NarrativeDungeonObjectiveQuery> BuildQueries(NarrativeState state, NarrativeCatalog catalog)
    {
        List<NarrativeDungeonObjectiveQuery> result = [];
        foreach (var progress in state.Quests.Where(q => q.Status == QuestStatus.Active && !state.WasStorySkipped(q.QuestId))
            .OrderBy(q => q.QuestId, StringComparer.Ordinal))
        {
            var definition = catalog.Quests[progress.QuestId];
            if (definition.DungeonObjectives is null || progress.DungeonObjectives is null) continue;
            foreach (var pair in definition.DungeonObjectives.OrderBy(p => p.Key, StringComparer.Ordinal))
                if (!progress.CompletedObjectives.Contains(pair.Key)
                    && progress.DungeonObjectives.TryGetValue(pair.Key, out var activated))
                    result.Add(new(progress.QuestId, pair.Key, pair.Value.DungeonId, pair.Value.Difficulty, activated.ActivatedAt,
                        [.. progress.DungeonObjectives.Values.Where(p => p.DungeonSessionId.HasValue).Select(p => p.DungeonSessionId!.Value)]));
        }
        return result;
    }

    public static void ValidateTargets(NarrativeCatalog catalog, DungeonContentCatalog content)
    {
        if (catalog.Quests.Values.SelectMany(q => q.DungeonObjectives?.Values ?? [])
            .Any(target => content.GetSelectionError(target.DungeonId, target.Difficulty) is not null)
            || catalog.Quests.Values.SelectMany(q => q.InteractionObjectives?.Values ?? [])
                .Any(target => content.GetSelectionError(target.DungeonId, target.Difficulty) is not null)
            || catalog.Quests.Values.SelectMany(q => q.KillObjectives?.Values ?? [])
                .Any(target => content.GetSelectionError(target.DungeonId, target.Difficulty) is not null))
            throw new InvalidOperationException("Narrative: Dungeon objective references an unavailable dungeon selection.");
    }

    public async Task<NarrativeView> RecordAsync(Guid characterId, string steamId, DungeonObjectiveCommand command,
        CancellationToken cancellationToken)
    {
        NarrativeCommand receipt = new(command.RequestId, command.ExpectedRevision, command.CatalogVersion,
            command.QuestId, command.ObjectiveId) { DungeonEvidenceSessionId = command.DungeonSessionId };
        receipt.Validate();
        if (characterId == Guid.Empty || command.DungeonSessionId == Guid.Empty)
            throw new NarrativeConflictException("invalid_narrative_command");
        var evidence = await evidenceRepository.LoadNarrativeDungeonClearEvidenceAsync(
            command.DungeonSessionId, characterId, cancellationToken);
        if (evidence is null || evidence.SteamId != steamId)
            throw new NarrativeConflictException("dungeon_clear_evidence_not_authorized");
        DateTimeOffset now = clock.GetUtcNow();
        // Receipt replay is checked by the repository before current revision/quest gates.
        // Evidence is immutable and still required on every call, including replay.
        NarrativeState committed = await repository.CommitNarrativeAsync(characterId, evidence.SteamId, receipt, state =>
        {
            NarrativeCoordinator.ValidateState(state, catalog);
            if (command.CatalogVersion != catalog.Version) throw new NarrativeConflictException("narrative_catalog_changed");
            if (!catalog.Quests.TryGetValue(command.QuestId, out var definition))
                throw new NarrativeConflictException("dungeon_objective_not_available");
            NarrativeState updated = quests.RecordDungeonObjective(state, definition, command.ObjectiveId, evidence, clock.GetUtcNow());
            return scenarios.Project(updated, catalog) with { Revision = checked(state.Revision + 1) };
        }, now, cancellationToken);
        return narrative.View(committed);
    }
}
