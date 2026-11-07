using System.Text.Json;

namespace ProjectRpg.Operations.Core.Monitoring;

public sealed record SettlementHealth(long Pending, long Processing, long Failed,
    DateTimeOffset? OldestOutstandingAtUtc, DateTimeOffset? LatestFailedUpdatedAtUtc);
public sealed record LogHealth(long DroppedCount, long SinkErrorCount, long RetainedBytes, DateTimeOffset? LastWriteAtUtc);
public sealed record ServiceHealth(string Mode);
public sealed record RuntimeStatus(DateTimeOffset ObservedAtUtc, string BootId, bool Ready, SettlementHealth Settlements, LogHealth Logs,
    ServiceHealth ServiceControl);
public interface ISettlementHealthReader
{
    Task<SettlementHealth> ReadAsync(CancellationToken ct);
}
public static class MonitoringJson
{
    public static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web) { RespectRequiredConstructorParameters = true };
}
public sealed record MonitorObservation(DateTimeOffset ObservedAtUtc, RuntimeStatus? Status, BackupObservation? Backup = null);
public sealed record AlertEvent(Guid EventId, string Environment, string Service, string IncidentKey, string Severity,
    string State, DateTimeOffset FirstSeenAtUtc, DateTimeOffset ObservedAtUtc, string Summary, IReadOnlyList<string> ReferenceIds);
public interface IMonitorProbe { Task<MonitorObservation> ObserveAsync(CancellationToken ct); }
public interface IAlertDelivery { Task<bool> SendAsync(AlertEvent alert, CancellationToken ct); }
public interface IMonitorStateStore
{
    Task<MonitorState> LoadAsync(CancellationToken ct);
    Task SaveAsync(MonitorState state, CancellationToken ct);
}
public sealed class MonitorState
{
    public int Version { get; init; } = 1;
    public string Environment { get; set; } = "";
    public string Service { get; set; } = "";
    public string? BootId { get; set; }
    public long LastDropped { get; set; }
    public long LastSinkErrors { get; set; }
    public long LastFailedCount { get; set; }
    public DateTimeOffset? LastFailedUpdatedAtUtc { get; set; }
    public DateTimeOffset? DrainObservedSinceUtc { get; set; }
    public DateTimeOffset? DrainLastObservedAtUtc { get; set; }
    public Dictionary<string, IncidentState> Incidents { get; init; } = new(StringComparer.Ordinal);
    public List<PendingAlert> Outbox { get; init; } = [];
}
public sealed class IncidentState
{
    public bool Active { get; set; }
    public int BadSamples { get; set; }
    public int GoodSamples { get; set; }
    public DateTimeOffset FirstSeenAtUtc { get; set; }
    public DateTimeOffset? LastEventAtUtc { get; set; }
    public string? LastSignal { get; set; }
}
public sealed class PendingAlert
{
    public required AlertEvent Event { get; init; }
    public int Attempts { get; set; }
    public DateTimeOffset NextAttemptAtUtc { get; set; }
}
