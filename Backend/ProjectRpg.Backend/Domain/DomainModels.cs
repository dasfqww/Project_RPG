namespace ProjectRpg.Backend.Domain;

public sealed record SteamIdentity(
    string SteamId,
    string OwnerSteamId,
    bool VacBanned,
    bool PublisherBanned);

public sealed record Roster(
    Guid RosterId,
    string SteamId,
    string WorldId,
    DateTimeOffset CreatedAt);

public sealed record GameCharacter(
    Guid CharacterId,
    Guid RosterId,
    string SteamId,
    string Name,
    DateTimeOffset CreatedAt);

public sealed record CharacterEconomyContext(
    Guid CharacterId,
    Guid RosterId,
    string SteamId);

public enum DungeonSessionState
{
    Waiting,
    Loading,
    InProgress,
    SettlementPending,
    Cleared,
    Failed,
    Closed
}

public sealed record DungeonSessionMember(
    Guid CharacterId,
    string SteamId,
    DateTimeOffset JoinedAt,
    DateTimeOffset LeaseExpiresAt);

public sealed record DungeonSession(
    Guid DungeonSessionId,
    string DungeonId,
    string Difficulty,
    DungeonSessionState State,
    string? ServerId,
    string? ServerAddress,
    DateTimeOffset CreatedAt,
    DateTimeOffset UpdatedAt,
    DateTimeOffset ExpiresAt,
    IReadOnlyList<DungeonSessionMember> Members);

public sealed record ConsumedJoinTicket(
    Guid DungeonSessionId,
    Guid CharacterId,
    string SteamId);

public sealed record InventoryItem(
    string ItemId,
    int Quantity,
    int SlotIndex,
    string Category,
    string InstanceId);

public enum PrincipalKind
{
    Player,
    GameServer,
    Administrator
}

public sealed record AuthenticatedPrincipal(
    PrincipalKind Kind,
    string? SteamId,
    string? ServerId,
    Guid? DungeonSessionId = null)
{
    public bool IsGameServer => Kind == PrincipalKind.GameServer;

    public bool IsAdministrator => Kind == PrincipalKind.Administrator;

    public bool IsTrustedService => IsAdministrator || IsGameServer;

    public bool MatchesGameServer(string serverId)
    {
        return IsGameServer
            && !string.IsNullOrWhiteSpace(ServerId)
            && string.Equals(ServerId, serverId, StringComparison.Ordinal);
    }

    public bool MatchesGameServer(
        string serverId,
        Guid dungeonSessionId)
    {
        return MatchesGameServer(serverId)
            && (!DungeonSessionId.HasValue
                || DungeonSessionId.Value == dungeonSessionId);
    }
}

public sealed record GameServerCredential(
    string ServerId,
    Guid DungeonSessionId);
