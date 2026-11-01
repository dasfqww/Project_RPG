namespace ProjectRpg.Backend.Domain;

// Server-authoritative expiry, distinct from a player's explicit abandonment.
public sealed record DungeonDisconnectionExpulsion(
    Guid DungeonSessionId,
    Guid CharacterId,
    string SteamId,
    DateTimeOffset DisconnectedAt,
    DateTimeOffset ExpiredAt,
    DateTimeOffset ProcessedAt);
