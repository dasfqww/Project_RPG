using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Infrastructure.Reads;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Sessions;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    internal Result<Page<CharacterHeader>> SearchOperationsCharacters(CharacterLookup query)
    {
        lock (_characterGate)
        {
            IEnumerable<GameCharacter> matches = _characters.Values.Where(c => query.Kind == CharacterLookupKind.SteamId
                ? c.SteamId == query.Value : c.CharacterId == Guid.Parse(query.Value));
            if (query.Page.After is { } after)
                matches = matches.Where(c => c.CreatedAt > after.Timestamp || (c.CreatedAt == after.Timestamp && c.CharacterId.CompareTo(after.Id) > 0));
            CharacterHeader[] rows = matches.OrderBy(c => c.CreatedAt).ThenBy(c => c.CharacterId)
                .Take(query.Page.Limit + 1).Select(OperationsHeader).ToArray();
            return Result<Page<CharacterHeader>>.Success(ReadMapping.Page(rows, query.Page.Limit, c => new(c.CharacterId, c.CreatedAtUtc)));
        }
    }
    internal Result<CharacterHeader> ReadOperationsCharacter(Guid id)
    {
        lock (_characterGate)
            return _characters.TryGetValue(id, out GameCharacter? character)
                ? Result<CharacterHeader>.Success(OperationsHeader(character)) : Result<CharacterHeader>.Fail(Failures.CharacterMissing);
    }
    private CharacterHeader OperationsHeader(GameCharacter c) => new(c.CharacterId, c.SteamId, c.RosterId, c.Name, c.CreatedAt,
        ReadMapping.Progression(CharacterProgressionSummary.FromStored(c.CharacterId, _progression.GetValueOrDefault(c.CharacterId))));

    internal Result<Page<SessionEvidence>> ReadOperationsSessions(Guid id, PageRequest page, DateTimeOffset now)
    {
        lock (_dungeonGate)
        lock (_characterGate)
        {
            if (!_characters.ContainsKey(id)) return Result<Page<SessionEvidence>>.Fail(Failures.CharacterMissing);
            IEnumerable<DungeonSession> sessions = _dungeonSessions.Values.Where(s => s.Members.Any(m => m.CharacterId == id));
            if (page.After is { } after)
                sessions = sessions.Where(s => s.UpdatedAt < after.Timestamp || (s.UpdatedAt == after.Timestamp && s.DungeonSessionId.CompareTo(after.Id) < 0));
            SessionEvidence[] rows = sessions.OrderByDescending(s => s.UpdatedAt).ThenByDescending(s => s.DungeonSessionId)
                .Take(page.Limit + 1).Select(s =>
                {
                    DungeonSessionMember member = s.Members.Single(m => m.CharacterId == id);
                    DateTimeOffset? lease = _characterLeases.TryGetValue(id, out CharacterLeaseRecord? stored)
                        && stored.DungeonSessionId == s.DungeonSessionId ? stored.ExpiresAt : null;
                    return new SessionEvidence(s.DungeonSessionId, s.DungeonId, s.Difficulty, s.State.ToString(), s.UpdatedAt,
                        s.ExpiresAt, member.LeaseExpiresAt, lease, SessionEvidence.Observe(now, s.ExpiresAt, member.LeaseExpiresAt, lease),
                        ReadMapping.Settlement(_dungeonRewardSettlements.GetValueOrDefault(s.DungeonSessionId)));
                }).ToArray();
            return Result<Page<SessionEvidence>>.Success(ReadMapping.Page(rows, page.Limit, s => new(s.DungeonSessionId, s.UpdatedAtUtc)));
        }
    }
}
