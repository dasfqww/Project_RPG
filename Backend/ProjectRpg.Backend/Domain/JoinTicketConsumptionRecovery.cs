namespace ProjectRpg.Backend.Domain;

public static class JoinTicketConsumptionRecovery
{
    // Ordinary admission-replay window, not a physical evidence TTL. Historical
    // consumption evidence remains for the session lifetime, without extending
    // membership, credentials, reconnect grace, or a server lease.
    public static readonly TimeSpan Retention = TimeSpan.FromMinutes(10);
}

// Historical operation evidence only. This is NOT permission to admit a player.
public sealed record JoinTicketConsumptionEvidence(
    Guid DungeonSessionId,
    string ServerId,
    Guid ConsumptionRequestId,
    Guid CharacterId,
    string SteamId,
    long ConnectionGeneration,
    DateTimeOffset ConsumedAt);

// Proof that an existing, unconsumed ticket was atomically removed and this
// intent fenced. Absence of both a ticket and a receipt cannot produce this.
public sealed record JoinTicketConsumptionCancellation(
    Guid DungeonSessionId,
    string ServerId,
    Guid ConsumptionRequestId,
    string TicketHash,
    string ExpectedSteamId,
    DateTimeOffset CancelledAt);

// Neither branch is admission permission. Null repository result is unresolved.
public sealed record JoinTicketConsumptionResolution(
    string State,
    JoinTicketConsumptionEvidence? Consumption,
    JoinTicketConsumptionCancellation? Cancellation);
