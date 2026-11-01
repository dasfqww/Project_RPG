namespace ProjectRpg.Backend.Domain;

public static class DungeonConnectionPolicy
{
    public static readonly TimeSpan UnexpectedDisconnectGrace = TimeSpan.FromMinutes(10);
    public static readonly TimeSpan MaximumObservationClockSkew = TimeSpan.FromSeconds(5);

    // Only the assigned, authenticated game server supplies this timestamp.
    // Small clock skew is clamped, never added to the backend receipt time.
    // Older senders without an observation retain the receipt-time contract.
    public static DateTimeOffset ResolveObservation(DateTimeOffset? observedAtUtc,
        DateTimeOffset joinedAt, DateTimeOffset now)
    {
        if (observedAtUtc is not DateTimeOffset observed) return now;
        if (now < joinedAt || observed.Offset != TimeSpan.Zero || observed == DateTimeOffset.MinValue
            || observed - now > MaximumObservationClockSkew
            || joinedAt - observed > MaximumObservationClockSkew)
            throw new ArgumentException("Invalid server disconnection observation.", nameof(observedAtUtc));
        return observed < joinedAt ? joinedAt : observed > now ? now : observed;
    }

    // No marker is not evidence of disconnection. Compare elapsed time rather
    // than adding to persisted timestamps, which can overflow on corrupt data.
    public static bool HasReconnectGraceExpired(DateTimeOffset? disconnectedAt, DateTimeOffset now)
        => disconnectedAt is DateTimeOffset first && now - first >= UnexpectedDisconnectGrace;
}
