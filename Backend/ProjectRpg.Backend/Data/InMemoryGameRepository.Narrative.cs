using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : INarrativeRepository
{
    private readonly Dictionary<Guid, NarrativeState> _narrative = new();
    private readonly Dictionary<(Guid CharacterId, Guid RequestId), (string Fingerprint, NarrativeState Result)>
        _narrativeCommands = new();

    private void RequireNarrativeOwner(Guid characterId, string steamId)
    {
        if (!_characters.TryGetValue(characterId, out var character) || character.SteamId != steamId)
            throw new NarrativeConflictException("character_not_owned");
    }

    public Task<NarrativeState> LoadNarrativeAsync(Guid characterId, string steamId, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            RequireNarrativeOwner(characterId, steamId);
            return Task.FromResult(_narrative.GetValueOrDefault(characterId) ?? NarrativeState.Create(characterId));
        }
    }

    public Task<NarrativeState> CommitNarrativeAsync(Guid characterId, string steamId, NarrativeCommand command,
        Func<NarrativeState, NarrativeState> transition, DateTimeOffset now, CancellationToken cancellationToken)
    {
        string fingerprint = command.Fingerprint();
        cancellationToken.ThrowIfCancellationRequested();
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            RequireNarrativeOwner(characterId, steamId);
            if (_narrativeCommands.TryGetValue((characterId, command.RequestId), out var receipt))
            {
                if (receipt.Fingerprint != fingerprint) throw new NarrativeConflictException("idempotency_conflict");
                return Task.FromResult(receipt.Result);
            }
            NarrativeState state = _narrative.GetValueOrDefault(characterId) ?? NarrativeState.Create(characterId);
            if (state.Revision != command.ExpectedRevision) throw new NarrativeConflictException("narrative_revision_conflict");
            NarrativeState updated = transition(state);
            if (updated.CharacterId != characterId || updated.Revision != state.Revision + 1)
                throw new NarrativeConflictException("invalid_narrative_transition");
            cancellationToken.ThrowIfCancellationRequested();
            _narrative[characterId] = updated;
            _narrativeCommands[(characterId, command.RequestId)] = (fingerprint, updated);
            return Task.FromResult(updated);
        }
    }
}
