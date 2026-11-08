using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using ProjectRpg.Backend.Data;

namespace ProjectRpg.Backend.Narrative;

public sealed record NarrativeInteractionCommand(Guid EventId, string TargetId, long ConnectionGeneration, DateTimeOffset ObservedAtUtc)
{
    // Internal adapter only: HTTP interaction requests cannot select another evidence domain.
    internal bool IsKillEvent { get; init; }
    public void Validate()
    {
        if (EventId == Guid.Empty || !NarrativeCommand.ValidId(TargetId) || ConnectionGeneration <= 0 || ObservedAtUtc == default)
            throw new NarrativeConflictException("invalid_interaction_event");
    }
    public string Fingerprint(Guid sessionId, Guid characterId)
    {
        Validate();
        return Convert.ToHexStringLower(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new {
            Kind = IsKillEvent ? "personal_kill_v1" : "personal_interaction_v1", SessionId = sessionId, CharacterId = characterId,
            TargetId, ConnectionGeneration, ObservedTicks = ObservedAtUtc.UtcTicks })));
    }
}
public sealed record NarrativeInteractionContext(string DungeonId, string Difficulty, DateTimeOffset ReceivedAt);
public sealed record RecordedInteractionObjective(string QuestId, string ObjectiveId);
public sealed record NarrativeInteractionResult(Guid EventId, Guid CharacterId, long Revision,
    ImmutableArray<RecordedInteractionObjective> AppliedObjectives);

/// <summary>Consumes an already approved personal world action from its assigned game server.</summary>
public sealed class NarrativeInteractionCoordinator(INarrativeInteractionRepository repository, NarrativeCatalog catalog,
    QuestManager quests, ScenarioManager scenarios, TimeProvider clock)
{
    public async Task<NarrativeInteractionResult> RecordAsync(Guid sessionId, Guid characterId, string serverId,
        NarrativeInteractionCommand command, CancellationToken ct)
    {
        command.Validate();
        if (sessionId == Guid.Empty || characterId == Guid.Empty || string.IsNullOrWhiteSpace(serverId))
            throw new NarrativeConflictException("invalid_interaction_event");
        var state = await repository.CommitNarrativeInteractionAsync(sessionId, characterId, serverId, command, (current, context) =>
        {
            NarrativeCoordinator.ValidateState(current, catalog);
            if (command.ObservedAtUtc > context.ReceivedAt || command.ObservedAtUtc < context.ReceivedAt.AddMinutes(-5))
                throw new NarrativeConflictException("interaction_event_out_of_window");
            var next = quests.RecordInteraction(current, catalog, command, context);
            if (ReferenceEquals(next, current)) return current;
            return scenarios.Project(next, catalog) with { Revision = checked(current.Revision + 1) };
        }, clock.GetUtcNow(), ct);
        // Replay renders the stored receipt, not current catalog eligibility or a new event.
        return new(command.EventId, characterId, state.Revision,
            [.. state.Quests.SelectMany(q => (q.InteractionObjectives ?? ImmutableDictionary<string, InteractionObjectiveProgress>.Empty)
                .Where(p => p.Value.EventId == command.EventId).Select(p => new RecordedInteractionObjective(q.QuestId, p.Key)))
                .OrderBy(p => p.QuestId, StringComparer.Ordinal).ThenBy(p => p.ObjectiveId, StringComparer.Ordinal)]);
    }
}
