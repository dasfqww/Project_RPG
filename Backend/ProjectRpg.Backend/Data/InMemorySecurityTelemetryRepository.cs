using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class InMemorySecurityTelemetryRepository
    : ISecurityTelemetryRepository
{
    private readonly object _gate = new();
    private readonly Dictionary<Guid, SecurityTelemetryEvent> _events = [];

    public Task<SecurityEventStoreResult> StoreBatchAsync(
        IReadOnlyList<SecurityTelemetryEvent> events,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            Guid[] conflicts = events
                .Where(value =>
                    _events.TryGetValue(
                        value.EventId,
                        out SecurityTelemetryEvent? existing)
                    && !string.Equals(
                        existing.Fingerprint,
                        value.Fingerprint,
                        StringComparison.Ordinal))
                .Select(value => value.EventId)
                .ToArray();
            if (conflicts.Length > 0)
            {
                return Task.FromResult(new SecurityEventStoreResult(
                    0,
                    0,
                    conflicts));
            }

            int acceptedCount = 0;
            int duplicateCount = 0;
            foreach (SecurityTelemetryEvent value in events)
            {
                if (_events.TryAdd(value.EventId, value))
                {
                    ++acceptedCount;
                }
                else
                {
                    ++duplicateCount;
                }
            }

            return Task.FromResult(new SecurityEventStoreResult(
                acceptedCount,
                duplicateCount,
                []));
        }
    }

    public Task<SecuritySessionSummary> GetSessionSummaryAsync(
        Guid dungeonSessionId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            SecurityTelemetryEvent[] events = _events.Values
                .Where(value => value.DungeonSessionId == dungeonSessionId)
                .ToArray();
            return Task.FromResult(SecurityTelemetrySummaryBuilder.Build(
                dungeonSessionId,
                events));
        }
    }
}

internal static class SecurityTelemetrySummaryBuilder
{
    public static SecuritySessionSummary Build(
        Guid dungeonSessionId,
        IReadOnlyList<SecurityTelemetryEvent> events)
    {
        return new SecuritySessionSummary(
            dungeonSessionId,
            events.Count,
            events.Sum(value => value.Score),
            events.Count == 0 ? 0.0 : events.Max(value => value.RiskAfter),
            events.Count == 0 ? null : events.Min(value => value.ReceivedAt),
            events.Count == 0 ? null : events.Max(value => value.ReceivedAt),
            events.GroupBy(value => value.Type)
                .OrderBy(group => group.Key)
                .Select(group => new SecurityTelemetryBreakdown(
                    group.Key.ToString(),
                    group.LongCount(),
                    group.Sum(value => value.Score)))
                .ToArray(),
            events.GroupBy(value => value.Severity)
                .OrderBy(group => group.Key)
                .Select(group => new SecurityTelemetryBreakdown(
                    group.Key.ToString(),
                    group.LongCount(),
                    group.Sum(value => value.Score)))
                .ToArray(),
            events.GroupBy(value => value.CharacterId)
                .OrderBy(group => group.Key)
                .Select(group => new SecurityCharacterBreakdown(
                    group.Key,
                    group.LongCount(),
                    group.Sum(value => value.Score),
                    group.Max(value => value.RiskAfter)))
                .ToArray());
    }
}
