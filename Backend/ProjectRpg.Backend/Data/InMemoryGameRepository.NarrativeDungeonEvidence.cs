using System.Collections.Immutable;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : INarrativeDungeonEvidenceRepository
{
    public Task<ImmutableArray<DungeonObjectiveCandidate>> FindNarrativeDungeonObjectiveCandidatesAsync(
        Guid characterId, string steamId, IReadOnlyList<NarrativeDungeonObjectiveQuery> queries, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_characterGate) RequireNarrativeOwner(characterId, steamId);
        lock (_dungeonGate)
        {
            var candidates = ImmutableArray.CreateBuilder<DungeonObjectiveCandidate>();
            var evidence = _dungeonRewardSettlements.Values.Where(s => s.CharacterIds.Contains(characterId))
                .OrderBy(s => s.CreatedAt).ThenBy(s => s.DungeonSessionId).ToArray();
            foreach (var query in queries)
            {
                var match = evidence.FirstOrDefault(s => s.CreatedAt >= query.ActivatedAt
                    && !query.ExcludedSessionIds.Contains(s.DungeonSessionId)
                    && _dungeonSessions.TryGetValue(s.DungeonSessionId, out var dungeon)
                    && dungeon.DungeonId == query.DungeonId && dungeon.Difficulty == query.Difficulty);
                if (match is not null) candidates.Add(new(query.QuestId, query.ObjectiveId, match.DungeonSessionId));
                if (candidates.Count == 256) break;
            }
            return Task.FromResult(candidates.ToImmutable());
        }
    }

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
