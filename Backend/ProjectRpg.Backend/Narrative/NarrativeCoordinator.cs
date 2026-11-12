using ProjectRpg.Backend.Data;

namespace ProjectRpg.Backend.Narrative;

/// <summary>Composition root for one narrative operation. Character state belongs to the repository.</summary>
public sealed class NarrativeCoordinator(INarrativeRepository repository, NarrativeCatalog catalog,
    QuestManager quests, ScenarioManager scenarios, DialogueManager dialogues, TimeProvider clock,
    INarrativeDungeonEvidenceRepository? dungeonEvidence = null)
{
    public async Task<NarrativeView> LoadAsync(Guid characterId, string steamId, CancellationToken cancellationToken)
    {
        NarrativeView view = View(await repository.LoadNarrativeAsync(characterId, steamId, cancellationToken));
        if (dungeonEvidence is null) return view;
        return view with { DungeonObjectiveCandidates = await dungeonEvidence.FindNarrativeDungeonObjectiveCandidatesAsync(
            characterId, steamId, DungeonObjectiveCoordinator.BuildQueries(view.State, catalog), cancellationToken) };
    }

    public async Task<NarrativeView> ChooseAsync(Guid characterId, string steamId, NarrativeCommand command,
        CancellationToken cancellationToken)
    {
        command.Validate();
        DateTimeOffset now = clock.GetUtcNow();
        NarrativeState committed = await repository.CommitNarrativeAsync(characterId, steamId, command, state =>
        {
            ValidateState(state, catalog);
            if (command.CatalogVersion != catalog.Version) throw new NarrativeConflictException("narrative_catalog_changed");
            if (command.ExpectedRevision != state.Revision) throw new NarrativeConflictException("narrative_revision_conflict");
            (DialogueDefinition dialogue, DialogueChoice choice) = dialogues.Resolve(state, catalog, command);
            if (dialogue.Informational) throw new NarrativeConflictException("informational_dialogue_not_mutable");
            NarrativeState updated = quests.Apply(state, catalog.Quests[dialogue.QuestId!], choice, clock.GetUtcNow());
            return scenarios.Project(updated, catalog) with { Revision = checked(state.Revision + 1) };
        }, now, cancellationToken);
        return View(committed);
    }

    internal NarrativeView View(NarrativeState state)
    {
        ValidateState(state, catalog);
        return new(catalog.Version, state, dialogues.GetAvailable(state, catalog),
            [.. state.Quests.Where(q => !state.WasStorySkipped(q.QuestId))
                .Select(q => QuestJournalEntry.From(q, catalog.Quests[q.QuestId]))]);
    }

    internal static void ValidateState(NarrativeState state, NarrativeCatalog catalog)
    {
        if (state.CharacterId == Guid.Empty || state.SchemaVersion != 1 || state.Revision < 0 || state.Revision == long.MaxValue
            || state.Quests.IsDefault || state.CompletedChapters.IsDefault || state.StoryUnlocks.IsDefault
            || state.Quests.Any(q => q is null)
            || state.Quests.Length != state.Quests.Select(q => q.QuestId).Distinct().Count())
            throw new NarrativeConflictException("invalid_narrative_state");
        NarrativeStorySkip.ValidateRecords(state, catalog);
        foreach (QuestProgress progress in state.Quests)
        {
            if (!catalog.Quests.TryGetValue(progress.QuestId, out QuestDefinition? definition)
                || definition.Version != progress.DefinitionVersion || !Enum.IsDefined(progress.Status)
                || progress.CompletedObjectives.IsDefault
                || progress.CompletedObjectives.Length != progress.CompletedObjectives.Distinct().Count()
                || !progress.CompletedObjectives.All(definition.Objectives.Contains)
                || !progress.CompletedObjectives.All(id => definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains))
                || (progress.Status == QuestStatus.Active) == (progress.CompletedObjectives.Length == definition.Objectives.Length)
                || !definition.Prerequisites.All(state.SatisfiesStoryQuest))
                throw new NarrativeConflictException("incompatible_narrative_state");
            var dungeonProgress = progress.DungeonObjectives;
            var dungeonDefinitions = definition.DungeonObjectives;
            if (dungeonProgress is not null && dungeonProgress.Any(pair => pair.Value is null
                    || dungeonDefinitions?.ContainsKey(pair.Key) != true
                    || !definition.RequiredObjectives(pair.Key).All(progress.CompletedObjectives.Contains)
                    || pair.Value.ActivatedAt == default
                    || pair.Value.DungeonSessionId == Guid.Empty
                    || progress.CompletedObjectives.Contains(pair.Key) != pair.Value.DungeonSessionId.HasValue)
                || dungeonDefinitions is not null && dungeonDefinitions.Keys.Any(id =>
                    definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains)
                    && dungeonProgress?.ContainsKey(id) != true)
                || dungeonProgress is not null && dungeonProgress.Values.Where(p => p.DungeonSessionId.HasValue)
                    .Select(p => p.DungeonSessionId).Distinct().Count()
                        != dungeonProgress.Values.Count(p => p.DungeonSessionId.HasValue))
                throw new NarrativeConflictException("incompatible_dungeon_objective_state");
        }
        foreach (var progress in state.Quests)
        {
            var definition = catalog.Quests[progress.QuestId];
            var entries = progress.InteractionObjectives;
            bool SameEventInPrerequisites(string id, Guid eventId)
            {
                Stack<string> pending = new(definition.RequiredObjectives(id)); HashSet<string> visited = new(StringComparer.Ordinal);
                while (pending.TryPop(out string? parent))
                {
                    if (!visited.Add(parent)) continue;
                    if (entries?.GetValueOrDefault(parent)?.EventId == eventId) return true;
                    foreach (string ancestor in definition.RequiredObjectives(parent)) pending.Push(ancestor);
                }
                return false;
            }
            if (entries is not null && entries.Any(pair => pair.Value is null
                    || definition.InteractionObjectives?.ContainsKey(pair.Key) != true
                    || !definition.RequiredObjectives(pair.Key).All(progress.CompletedObjectives.Contains)
                    || pair.Value.ActivatedAt == default || pair.Value.EventId == Guid.Empty
                    || progress.CompletedObjectives.Contains(pair.Key) != pair.Value.EventId.HasValue
                    || pair.Value.EventId is { } eventId && SameEventInPrerequisites(pair.Key, eventId))
                || definition.InteractionObjectives is not null && definition.InteractionObjectives.Keys.Any(id =>
                    definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains) && entries?.ContainsKey(id) != true))
                throw new NarrativeConflictException("incompatible_interaction_objective_state");
        }
        foreach (QuestProgress progress in state.Quests)
        {
            var definition = catalog.Quests[progress.QuestId]; var entries = progress.KillObjectives;
            bool ReusedAncestor(string id, Guid eventId)
            {
                Stack<string> pending = new(definition.RequiredObjectives(id)); HashSet<string> seen = new(StringComparer.Ordinal);
                while (pending.TryPop(out string? parent))
                {
                    if (!seen.Add(parent)) continue;
                    if (entries?.GetValueOrDefault(parent)?.LastEventId == eventId) return true;
                    foreach (string ancestor in definition.RequiredObjectives(parent)) pending.Push(ancestor);
                }
                return false;
            }
            if (entries is not null && entries.Any(pair => pair.Value is null
                    || definition.KillObjectives?.GetValueOrDefault(pair.Key) is not { } target
                    || !definition.RequiredObjectives(pair.Key).All(progress.CompletedObjectives.Contains)
                    || pair.Value.ActivatedAt == default || pair.Value.RequiredCount != target.RequiredCount
                    || pair.Value.Count < 0 || pair.Value.Count > target.RequiredCount
                    || pair.Value.LastEventId == Guid.Empty || (pair.Value.Count > 0) != pair.Value.LastEventId.HasValue
                    || progress.CompletedObjectives.Contains(pair.Key) != (pair.Value.Count == target.RequiredCount)
                    || pair.Value.LastEventId is { } eventId && ReusedAncestor(pair.Key, eventId))
                || definition.KillObjectives is not null && definition.KillObjectives.Keys.Any(id =>
                    definition.RequiredObjectives(id).All(progress.CompletedObjectives.Contains) && entries?.ContainsKey(id) != true))
                throw new NarrativeConflictException("incompatible_kill_objective_state");
        }
        NarrativeState projected = new ScenarioManager().Project(state, catalog);
        if (!state.CompletedChapters.SequenceEqual(projected.CompletedChapters)
            || !state.StoryUnlocks.SequenceEqual(projected.StoryUnlocks))
            throw new NarrativeConflictException("inconsistent_story_unlocks");
    }
}
