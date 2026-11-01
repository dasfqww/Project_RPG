namespace ProjectRpg.Backend.Domain;

// One consistent, read-only view of an assigned session. Members are persisted
// membership records, NOT admission grants. Only a matching immutable receipt
// proves explicit abandonment. Expulsions separately prove a processed
// unexpected-disconnect deadline; missing members or failed reads prove neither.
public sealed record DungeonMembershipSnapshot(
    Guid DungeonSessionId,
    string ServerId,
    DungeonSessionState State,
    DateTimeOffset UpdatedAt,
    DateTimeOffset ExpiresAt,
    IReadOnlyList<DungeonSessionMember> Members,
    IReadOnlyList<DungeonAbandonment> Abandonments,
    IReadOnlyList<DungeonDisconnectionExpulsion> DisconnectionExpulsions,
    DateTimeOffset? ObservedAt = null);
