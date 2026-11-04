using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IJoinTicketRepository
{
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
}
