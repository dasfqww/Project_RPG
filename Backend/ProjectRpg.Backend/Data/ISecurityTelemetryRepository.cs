using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface ISecurityTelemetryRepository
{
    Task<SecurityEventStoreResult> StoreBatchAsync(
        IReadOnlyList<SecurityTelemetryEvent> events,
        CancellationToken cancellationToken);

    Task<SecuritySessionSummary> GetSessionSummaryAsync(
        Guid dungeonSessionId,
        CancellationToken cancellationToken);
}
