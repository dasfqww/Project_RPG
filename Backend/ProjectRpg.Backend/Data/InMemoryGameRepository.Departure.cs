using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    public Task<DungeonSession> SetDungeonDepartureAsync(Guid dungeonSessionId, string steamId, Guid characterId,
        Guid requestId, long expectedRevision, bool cancel, int minimumPartySize, DateTimeOffset now, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession updated = DungeonDeparturePolicy.Apply(GetRequiredDungeonSession(dungeonSessionId),
                steamId, characterId, requestId, expectedRevision, cancel, minimumPartySize, now);
            _dungeonSessions[dungeonSessionId] = updated;
            return Task.FromResult(updated);
        }
    }
}
