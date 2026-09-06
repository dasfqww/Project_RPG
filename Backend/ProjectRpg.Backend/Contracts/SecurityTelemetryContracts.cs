namespace ProjectRpg.Backend.Contracts;

public sealed record SecurityEventBatchRequest(
    Guid DungeonSessionId,
    IReadOnlyList<SecurityEventContract?>? Events);

public sealed record SecurityEventContract(
    Guid EventId,
    Guid CharacterId,
    string? SteamId,
    string? Type,
    string? Severity,
    double Score,
    double RiskAfter,
    double ServerTimeSeconds,
    string? Detail);

public sealed record SecurityEventBatchResponse(
    int AcceptedCount,
    int DuplicateCount);

public sealed record SecurityTelemetryBreakdownContract(
    string Key,
    long EventCount,
    double TotalScore);

public sealed record SecurityCharacterBreakdownContract(
    Guid CharacterId,
    long EventCount,
    double TotalScore,
    double MaximumRiskAfter);

public sealed record SecuritySessionSummaryResponse(
    Guid DungeonSessionId,
    long TotalEvents,
    double TotalScore,
    double MaximumRiskAfter,
    DateTimeOffset? FirstReceivedAt,
    DateTimeOffset? LastReceivedAt,
    IReadOnlyList<SecurityTelemetryBreakdownContract> ByType,
    IReadOnlyList<SecurityTelemetryBreakdownContract> BySeverity,
    IReadOnlyList<SecurityCharacterBreakdownContract> ByCharacter);
