using System.Collections.Immutable;
using ProjectRpg.Backend.Data;

namespace ProjectRpg.Backend.Narrative;

/// <summary>Exactly one trusted, eligible personal death observation; never a client-supplied count.</summary>
public sealed record NarrativeKillCommand(Guid EventId, string EnemyId, long ConnectionGeneration, DateTimeOffset ObservedAtUtc)
{
    public void Validate()
    {
        if (EventId == Guid.Empty || !NarrativeCommand.ValidId(EnemyId) || ConnectionGeneration <= 0 || ObservedAtUtc == default)
            throw new NarrativeConflictException("invalid_kill_event");
    }
    // Reuse the same atomic session/member/lease/character fences and receipt store.
    internal NarrativeInteractionCommand StorageEvent() => new(EventId, EnemyId, ConnectionGeneration, ObservedAtUtc) { IsKillEvent = true };
}
public sealed record RecordedKillObjective(string QuestId, string ObjectiveId, int Count, int RequiredCount);
public sealed record NarrativeKillResult(Guid EventId, Guid CharacterId, long Revision,
    ImmutableArray<RecordedKillObjective> AppliedObjectives);

public sealed class NarrativeKillCoordinator(INarrativeInteractionRepository repository, NarrativeCatalog catalog,
    QuestManager quests, ScenarioManager scenarios, TimeProvider clock)
{
    public async Task<NarrativeKillResult> RecordAsync(Guid sessionId, Guid characterId, string serverId,
        NarrativeKillCommand command, CancellationToken ct)
    {
        command.Validate();
        if (sessionId == Guid.Empty || characterId == Guid.Empty || string.IsNullOrWhiteSpace(serverId))
            throw new NarrativeConflictException("invalid_kill_event");
        NarrativeState state;
        try
        {
            state = await repository.CommitNarrativeInteractionAsync(sessionId, characterId, serverId, command.StorageEvent(), (current, context) =>
            {
                NarrativeCoordinator.ValidateState(current, catalog);
                if (command.ObservedAtUtc > context.ReceivedAt || command.ObservedAtUtc < context.ReceivedAt.AddMinutes(-5))
                    throw new NarrativeConflictException("kill_event_out_of_window");
                var next = quests.RecordKill(current, catalog, command, context);
                return ReferenceEquals(next, current) ? current : scenarios.Project(next, catalog) with { Revision = checked(current.Revision + 1) };
            }, clock.GetUtcNow(), ct);
        }
        catch (NarrativeConflictException error) when (error.Message == "interaction_not_authorized")
        { throw new NarrativeConflictException("kill_not_authorized"); }
        // Render the stored historical receipt. Definition edits cannot rewrite replayed counts.
        return new(command.EventId, characterId, state.Revision,
            [.. state.Quests.SelectMany(q => (q.KillObjectives ?? ImmutableDictionary<string, KillObjectiveProgress>.Empty)
                .Where(p => p.Value.LastEventId == command.EventId)
                .Select(p => new RecordedKillObjective(q.QuestId, p.Key, p.Value.Count, p.Value.RequiredCount)))
                .OrderBy(p => p.QuestId, StringComparer.Ordinal).ThenBy(p => p.ObjectiveId, StringComparer.Ordinal)]);
    }
}
