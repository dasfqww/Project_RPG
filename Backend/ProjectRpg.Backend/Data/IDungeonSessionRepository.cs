using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IDungeonSessionRepository : IDungeonMembershipRepository
{
    Task<DungeonSession?> GetDungeonSessionAsync(
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<DungeonMembershipSnapshot?> GetDungeonMembershipSnapshotAsync(
        Guid dungeonSessionId,
        string serverId,
        CancellationToken cancellationToken);

    Task<DungeonSession?> GetActiveDungeonSessionForCharacterAsync(
        Guid characterId,
        string steamId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<DungeonSession> CreateDungeonSessionAsync(
        string steamId,
        Guid characterId,
        string dungeonId,
        string difficulty,
        DateTimeOffset now,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    Task<DungeonSession> JoinDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        int maxPartySize,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken);

    Task<DungeonSession> LeaveDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<DungeonSession> AssignDungeonServerAsync(
        Guid dungeonSessionId,
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken,
        bool requireDeparture = false,
        int minimumPartySize = 1);

    Task<DungeonSession> SetDungeonDepartureAsync(Guid dungeonSessionId, string steamId, Guid characterId,
        Guid requestId, long expectedRevision, bool cancel, int minimumPartySize,
        DateTimeOffset now, CancellationToken cancellationToken);

    Task<DungeonAbandonment> AbandonDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    // Assigned-server-only report. Current-generation tombstone makes a late
    // admission confirmation unable to revive a socket already reported lost.
    // An optional server observation survives delivery delays; now remains the
    // backend authorization/expiry clock and is never replaced by that observation.
    Task<DateTimeOffset> RecordDungeonMemberDisconnectionAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        long connectionGeneration,
        DateTimeOffset now,
        CancellationToken cancellationToken,
        DateTimeOffset? observedAtUtc = null);

    // Idempotently removes only members whose recorded disconnect reached the
    // grace deadline. Returns all immutable expiry receipts for this session.
    Task<IReadOnlyList<DungeonDisconnectionExpulsion>> ExpelDisconnectedDungeonMembersAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    // Only the assigned game server can confirm completed UE admission. A
    // duplicate/late confirmation must not clear a disconnect on that socket.
    Task<bool> ConfirmDungeonMemberAdmissionAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        long connectionGeneration,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<DungeonSession?> ClaimNextDungeonSessionAsync(
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken,
        IReadOnlyList<DungeonContentSelection>? allowedSelections = null,
        bool requireDeparture = false,
        int minimumPartySize = 1);

    Task<DungeonSession> StartDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken);

    Task<DungeonSession> HeartbeatDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken);

    Task<DungeonSession> FinishDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DungeonSessionState outcome,
        DateTimeOffset now,
        CancellationToken cancellationToken);
}
