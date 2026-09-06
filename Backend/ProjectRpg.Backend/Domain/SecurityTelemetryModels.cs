namespace ProjectRpg.Backend.Domain;

public enum SecurityEventType
{
    MovementSpeed,
    MovementDiscontinuity,
    AbilityActivationRate,
    InvalidTargetData,
    InvalidCombatHit,
    InvalidDamage,
    RestrictedActionAttempt,
    EnforcementStateChanged,
    PlayerRemoval
}

public enum SecurityEventSeverity
{
    Low,
    Medium,
    High,
    Critical
}

/** An immutable, server-authored security observation. */
public sealed record SecurityTelemetryEvent(
    Guid EventId,
    Guid DungeonSessionId,
    Guid CharacterId,
    string SteamId,
    string ServerId,
    SecurityEventType Type,
    SecurityEventSeverity Severity,
    double Score,
    double RiskAfter,
    double ServerTimeSeconds,
    string Detail,
    string Fingerprint,
    DateTimeOffset ReceivedAt);

public sealed record SecurityTelemetryMember(
    Guid CharacterId,
    string SteamId);

public sealed record SecurityEventStoreResult(
    int AcceptedCount,
    int DuplicateCount,
    IReadOnlyList<Guid> ConflictingEventIds);

public sealed record SecurityTelemetryBreakdown(
    string Key,
    long EventCount,
    double TotalScore);

public sealed record SecurityCharacterBreakdown(
    Guid CharacterId,
    long EventCount,
    double TotalScore,
    double MaximumRiskAfter);

public sealed record SecuritySessionSummary(
    Guid DungeonSessionId,
    long TotalEvents,
    double TotalScore,
    double MaximumRiskAfter,
    DateTimeOffset? FirstReceivedAt,
    DateTimeOffset? LastReceivedAt,
    IReadOnlyList<SecurityTelemetryBreakdown> ByType,
    IReadOnlyList<SecurityTelemetryBreakdown> BySeverity,
    IReadOnlyList<SecurityCharacterBreakdown> ByCharacter);

public static class SecurityTelemetryRules
{
    public const int MaximumBatchSize = 64;
    public const int MaximumDetailLength = 512;
    public const double MaximumScore = 100_000.0;

    public static bool TryValidate(
        SecurityTelemetryEvent securityEvent,
        out string error)
    {
        if (securityEvent.EventId == Guid.Empty
            || securityEvent.DungeonSessionId == Guid.Empty
            || securityEvent.CharacterId == Guid.Empty)
        {
            error = "EventId, DungeonSessionId, and CharacterId must be non-empty UUIDs.";
            return false;
        }

        if (securityEvent.SteamId.Length is < 1 or > 20
            || !securityEvent.SteamId.All(char.IsAsciiDigit))
        {
            error = "SteamId must contain 1 to 20 ASCII digits.";
            return false;
        }

        if (!EconomyRules.IsSimpleIdentifier(securityEvent.ServerId, 128))
        {
            error = "ServerId must be a simple identifier of at most 128 characters.";
            return false;
        }

        if (!double.IsFinite(securityEvent.Score)
            || securityEvent.Score < 0.0
            || securityEvent.Score > MaximumScore
            || !double.IsFinite(securityEvent.RiskAfter)
            || securityEvent.RiskAfter < 0.0
            || securityEvent.RiskAfter > MaximumScore
            || !double.IsFinite(securityEvent.ServerTimeSeconds)
            || securityEvent.ServerTimeSeconds < 0.0)
        {
            error = "Security scores and server time must be finite and within their allowed ranges.";
            return false;
        }

        if (securityEvent.Detail.Length > MaximumDetailLength
            || securityEvent.Detail.Any(char.IsControl))
        {
            error = $"Detail must contain at most {MaximumDetailLength} non-control characters.";
            return false;
        }

        if (securityEvent.Fingerprint.Length != 64
            || !securityEvent.Fingerprint.All(char.IsAsciiHexDigit))
        {
            error = "Fingerprint must be a SHA-256 hexadecimal digest.";
            return false;
        }

        error = string.Empty;
        return true;
    }
}
