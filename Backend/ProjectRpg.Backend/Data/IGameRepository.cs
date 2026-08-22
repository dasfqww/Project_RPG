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

    Task StoreGameServerCredentialAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    Task<GameServerCredential?> ResolveGameServerCredentialAsync(
        string tokenHash,
        DateTimeOffset now,
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
        CancellationToken cancellationToken);

    Task<DungeonSession?> ClaimNextDungeonSessionAsync(
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken);

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

    Task StoreJoinTicketAsync(
        string tokenHash,
        string steamId,
        Guid characterId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    Task<ConsumedJoinTicket?> ConsumeJoinTicketAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<IReadOnlyList<InventoryItem>> LoadInventoryAsync(
        Guid characterId,
        CancellationToken cancellationToken);

    Task SaveInventoryAsync(
        Guid characterId,
        IReadOnlyList<InventoryItem> inventory,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken);
}
