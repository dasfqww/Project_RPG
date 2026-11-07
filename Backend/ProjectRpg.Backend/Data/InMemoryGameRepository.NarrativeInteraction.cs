using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : INarrativeInteractionRepository
{
    public Task<NarrativeState> CommitNarrativeInteractionAsync(Guid sessionId, Guid characterId, string serverId,
        NarrativeInteractionCommand command, Func<NarrativeState, NarrativeInteractionContext, NarrativeState> transition,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        string fingerprint = command.Fingerprint(sessionId, characterId);
        cancellationToken.ThrowIfCancellationRequested();
        // Existing account/session paths use dungeon -> character order as well.
        lock (_dungeonGate)
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!_dungeonSessions.TryGetValue(sessionId, out var session) || session.ServerId != serverId
                || !_characters.TryGetValue(characterId, out var character))
                throw new NarrativeConflictException("interaction_not_authorized");
            if (_narrativeCommands.TryGetValue((characterId, command.EventId), out var receipt))
            {
                if (receipt.Fingerprint != fingerprint) throw new NarrativeConflictException("idempotency_conflict");
                return Task.FromResult(receipt.Result);
            }
            now = connectionClock?.GetUtcNow() ?? now;
            var member = session.Members.SingleOrDefault(m => m.CharacterId == characterId && m.SteamId == character.SteamId);
            if (session.State != DungeonSessionState.InProgress || session.ExpiresAt <= now
                || member is null || member.LeaseExpiresAt <= now || member.DisconnectedAt is not null
                || member.ConnectionGeneration != command.ConnectionGeneration
                || member.ConfirmedConnectionGeneration != command.ConnectionGeneration
                || command.ObservedAtUtc < member.JoinedAt
                || !_characterLeases.TryGetValue(characterId, out var lease) || lease.DungeonSessionId != sessionId || lease.ExpiresAt <= now)
                throw new NarrativeConflictException("interaction_not_authorized");
            var current = _narrative.GetValueOrDefault(characterId) ?? NarrativeState.Create(characterId);
            var next = transition(current, new(session.DungeonId, session.Difficulty, now));
            if (next.CharacterId != characterId || next.Revision != current.Revision && next.Revision != current.Revision + 1
                || next.Revision == current.Revision && !ReferenceEquals(next, current))
                throw new NarrativeConflictException("invalid_narrative_transition");
            cancellationToken.ThrowIfCancellationRequested();
            _narrative[characterId] = next;
            _narrativeCommands[(characterId, command.EventId)] = (fingerprint, next);
            return Task.FromResult(next);
        }
    }
}
