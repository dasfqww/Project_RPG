using System.Collections.Immutable;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : INarrativeStorySkipRepository
{
    private NarrativeStorySkipContext StorySkipContext(Guid characterId, string steamId)
    {
        RequireNarrativeOwner(characterId, steamId);
        var states = _characters.Values.Where(c => c.SteamId == steamId).OrderBy(c => c.CharacterId)
            .Select(c => _narrative.GetValueOrDefault(c.CharacterId) ?? NarrativeState.Create(c.CharacterId)).ToImmutableArray();
        bool busy = _dungeonSessions.Values.Any(s => s.State is not (DungeonSessionState.Cleared
            or DungeonSessionState.Failed or DungeonSessionState.Closed) && s.Members.Any(m => m.CharacterId == characterId));
        return new(states, busy);
    }
    public Task<NarrativeStorySkipContext> LoadStorySkipContextAsync(Guid characterId, string steamId, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (_dungeonGate) lock (_characterGate)
        {
            ct.ThrowIfCancellationRequested();
            return Task.FromResult(StorySkipContext(characterId, steamId));
        }
    }
    public Task<NarrativeState> CommitStorySkipAsync(Guid characterId, string steamId, StorySkipCommand command,
        Func<NarrativeStorySkipContext, NarrativeState> transition, DateTimeOffset now, CancellationToken ct)
    {
        string fingerprint = command.Fingerprint(); ct.ThrowIfCancellationRequested();
        lock (_dungeonGate) lock (_characterGate)
        {
            ct.ThrowIfCancellationRequested(); RequireNarrativeOwner(characterId, steamId);
            if (_narrativeCommands.TryGetValue((characterId, command.RequestId), out var receipt))
            {
                if (receipt.Fingerprint != fingerprint) throw new NarrativeConflictException("idempotency_conflict");
                return Task.FromResult(receipt.Result);
            }
            var context = StorySkipContext(characterId, steamId);
            var state = context.AccountStates.Single(s => s.CharacterId == characterId);
            if (state.Revision != command.ExpectedRevision) throw new NarrativeConflictException("narrative_revision_conflict");
            var updated = transition(context);
            if (updated.CharacterId != characterId || updated.Revision != checked(state.Revision + 1))
                throw new NarrativeConflictException("invalid_narrative_transition");
            ct.ThrowIfCancellationRequested();
            _narrative[characterId] = updated; _narrativeCommands[(characterId, command.RequestId)] = (fingerprint, updated);
            return Task.FromResult(updated);
        }
    }
}
