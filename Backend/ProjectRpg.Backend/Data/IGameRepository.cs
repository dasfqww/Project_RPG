using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IGameRepository
{
    Task EnsureCreatedAsync(CancellationToken cancellationToken);

    Task UpsertAccountAsync(
        SteamIdentity identity,
        DateTimeOffset authenticatedAt,
        CancellationToken cancellationToken);

    Task StoreSessionAsync(
        string tokenHash,
        string steamId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    Task<string?> ResolveSessionAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task RevokeSessionAsync(string tokenHash, string steamId, CancellationToken cancellationToken);

    Task StoreGameServerCredentialAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    Task<GameServerCredential?> ResolveGameServerCredentialAsync(
        string tokenHash,
        DateTimeOffset now,
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken);

    Task<IReadOnlyList<GameCharacter>> GetCharactersAsync(
        string steamId,
        CancellationToken cancellationToken);

    Task<IReadOnlyList<Roster>> GetRostersAsync(
        string steamId,
        CancellationToken cancellationToken);

    Task<GameCharacter> CreateCharacterAsync(
        string steamId,
        string name,
        DateTimeOffset createdAt,
        CancellationToken cancellationToken);

    Task<string?> GetCharacterOwnerAsync(
        Guid characterId,
        CancellationToken cancellationToken);

    Task<CharacterEconomyContext?> GetCharacterEconomyContextAsync(
        Guid characterId,
        CancellationToken cancellationToken);

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

    Task<DungeonRewardSettlement> EnqueueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string serverId,
        string rewardVersion,
        string commandFingerprint,
        IReadOnlyList<CurrencyChange> changes,
        IReadOnlyList<DungeonItemReward> itemRewards,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<DungeonRewardSettlement?> ClaimNextDungeonRewardSettlementAsync(
        string workerId,
        DateTimeOffset now,
        DateTimeOffset processingExpiresAt,
        CancellationToken cancellationToken);

    Task CompleteDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task FailDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task RequeueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        DateTimeOffset nextAttemptAt,
        CancellationToken cancellationToken);

    Task<bool> IsActiveDungeonSessionMemberAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<bool> IsAuthorizedGameServerSessionMemberAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<bool> AreAuthorizedSecurityTelemetryMembersAsync(
        Guid dungeonSessionId,
        string serverId,
        IReadOnlyList<SecurityTelemetryMember> members,
        DateTimeOffset now,
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken);

    // Validate membership and reconnect eligibility under the same lock as
    // abandonment, admission and settlement, then persist the ticket atomically.
    Task<JoinTicketIssueResult> StoreJoinTicketAsync(
        string tokenHash,
        string steamId,
        Guid characterId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    // A nonempty optional ID recovers the same consumption result while its
    // original generation and all active-member/lease checks still hold.
    // Omitting it preserves the legacy strictly one-use behavior.
    Task<ConsumedJoinTicket?> ConsumeJoinTicketAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken,
        Guid? consumptionRequestId = null);

    // Read-only evidence, including expired/superseded admission results. A
    // missing record is unresolved, never proof of a transaction not committing.
    Task<JoinTicketConsumptionEvidence?> ReadJoinTicketConsumptionEvidenceAsync(
        string tokenHash, string serverId, Guid dungeonSessionId, Guid consumptionRequestId,
        string expectedSteamId, DateTimeOffset now, CancellationToken cancellationToken);

    // Serialize with consume: return recorded history, or cancel a still-present
    // ticket and fence its ID/hash atomically. Missing history alone is unresolved.
    Task<JoinTicketConsumptionResolution?> SealJoinTicketConsumptionAsync(
        string tokenHash, string serverId, Guid dungeonSessionId, Guid consumptionRequestId,
        string expectedSteamId, DateTimeOffset now, CancellationToken cancellationToken);

    Task<IReadOnlyList<InventoryItem>> LoadInventoryAsync(
        Guid characterId,
        CancellationToken cancellationToken);

    Task SaveInventoryAsync(
        Guid characterId,
        IReadOnlyList<InventoryItem> inventory,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken,
        // HTTP writes must supply their authenticated assignment. Null is for
        // trusted in-process legacy import/fixtures, never a request-body option.
        GameServerCredential? requiredServer = null);
}
