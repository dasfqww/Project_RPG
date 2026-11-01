namespace ProjectRpg.Backend.Domain;

// Immutable acknowledgement, not the current state of the remaining party.
// Retained after member removal so retries cannot affect a newer character lease.
public sealed record DungeonAbandonment(
    Guid DungeonSessionId,
    Guid CharacterId,
    string SteamId,
    DateTimeOffset AbandonedAt,
    DungeonSessionState SessionStateAtAbandonment);
