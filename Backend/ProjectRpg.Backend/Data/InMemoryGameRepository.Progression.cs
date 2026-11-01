using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : ICharacterProgressionRepository
{
    private readonly Dictionary<Guid, CharacterProgressionState> _progression = new();
    private readonly HashSet<(Guid CharacterId, Guid SessionId)> _progressionCredits = new();
    private readonly Dictionary<(Guid CharacterId, Guid RequestId), (string Fingerprint, CharacterProgressionState Result)>
        _progressionCommands = new();

    public Task<IReadOnlyDictionary<Guid, CharacterProgressionSummary>> GetStoredProgressionSummariesAsync(
        string steamId, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            IReadOnlyDictionary<Guid, CharacterProgressionSummary> summaries = _characters.Values
                .Where(character => character.SteamId == steamId)
                .ToDictionary(character => character.CharacterId, character =>
                    CharacterProgressionSummary.FromStored(character.CharacterId,
                        _progression.GetValueOrDefault(character.CharacterId)));
            return Task.FromResult(summaries);
        }
    }

    public Task<CharacterProgressionState?> LoadProgressionAsync(Guid characterId, DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(LoadProgressionLocked(characterId) is { } state ? FirstSliceProgression.Copy(state) : null);
        }
    }

    private CharacterProgressionState? LoadProgressionLocked(Guid characterId)
    {
        if (!_progression.TryGetValue(characterId, out CharacterProgressionState? state)) return null;
        state = FirstSliceProgression.MigrateStoredState(state);
        List<(Guid CharacterId, Guid SessionId)> credits = [];
        foreach (DungeonRewardSettlement settlement in _dungeonRewardSettlements.Values
            .Where(value => value.State == DungeonRewardSettlementState.Completed && value.CompletedAt.HasValue
                && value.CharacterIds.Contains(characterId) && value.RewardVersion == FirstSliceProgression.RewardVersion)
            .OrderBy(value => value.CreatedAt).ThenBy(value => value.DungeonSessionId))
        {
            if (_progressionCredits.Contains((characterId, settlement.DungeonSessionId))
                || !_dungeonSessions.TryGetValue(settlement.DungeonSessionId, out DungeonSession? session)) continue;
            if (!FirstSliceProgression.TryCredit(state, session.DungeonId, session.Difficulty, settlement.RewardVersion,
                    out CharacterProgressionState updated)) continue;
            credits.Add((characterId, settlement.DungeonSessionId));
            state = updated;
        }
        _progression[characterId] = state;
        _progressionCredits.UnionWith(credits);
        return state;
    }

    public Task<CharacterProgressionState> CommitSkillConfigurationAsync(Guid characterId, string steamId,
        SkillConfigurationCommand command, DateTimeOffset now, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string fingerprint = FirstSliceProgression.Fingerprint(command);
        lock (_dungeonGate)
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!_characters.TryGetValue(characterId, out GameCharacter? character) || character.SteamId != steamId)
                throw new ProgressionConflictException("character_not_owned");
            if (_progressionCommands.TryGetValue((characterId, command.RequestId), out var receipt))
            {
                if (receipt.Fingerprint != fingerprint) throw new ProgressionConflictException("idempotency_conflict");
                return Task.FromResult(FirstSliceProgression.Copy(receipt.Result));
            }
            CharacterProgressionState state = LoadProgressionLocked(characterId)
                ?? throw new ProgressionConflictException("progression_not_initialized");
            if (_characterLeases.TryGetValue(characterId, out CharacterLeaseRecord? lease) && lease.ExpiresAt > now)
                throw new ProgressionConflictException("active_dungeon_configuration_locked");
            CharacterProgressionState updated = FirstSliceProgression.Configure(state, command);
            _progression[characterId] = updated;
            _progressionCommands[(characterId, command.RequestId)] = (fingerprint, FirstSliceProgression.Copy(updated));
            return Task.FromResult(FirstSliceProgression.Copy(updated));
        }
    }
}
