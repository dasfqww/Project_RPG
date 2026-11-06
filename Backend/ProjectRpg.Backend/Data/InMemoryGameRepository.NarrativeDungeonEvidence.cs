namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : INarrativeDungeonEvidenceRepository
{
    public async Task<NarrativeDungeonClearEvidence?> LoadNarrativeDungeonClearEvidenceAsync(
        Guid dungeonSessionId, Guid characterId, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        NarrativeDungeonClearEvidence evidence;
        lock (_dungeonGate)
        {
            if (!_dungeonRewardSettlements.TryGetValue(dungeonSessionId, out var settlement)
                || !settlement.CharacterIds.Contains(characterId)
                || !_dungeonSessions.TryGetValue(dungeonSessionId, out var session)) return null;
            evidence = new(dungeonSessionId, settlement.ServerId, characterId, string.Empty,
                session.DungeonId, session.Difficulty, settlement.CreatedAt);
        }
        // Do not nest dungeon and character locks. The narrative commit rechecks ownership.
        var owner = await GetCharacterEconomyContextAsync(characterId, cancellationToken);
        return owner is null ? null : evidence with { SteamId = owner.SteamId };
    }
}
