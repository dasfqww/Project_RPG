namespace ProjectRpg.Operations.Core.Monitoring;

public sealed record BackupStatus(int ContractVersion, string Environment, string Service, DateTimeOffset ObservedAtUtc,
    bool CollectionSucceeded, bool RepositoryHealthy, bool FullBackupFailed, bool IncrementalBackupFailed,
    DateTimeOffset? LastFullAtUtc, DateTimeOffset? LastIncrementalAtUtc, int FullChains, bool ArchiveConfigured,
    DateTimeOffset? LastArchivedAtUtc, DateTimeOffset? LastArchiveFailureAtUtc, long PendingWalCount,
    DateTimeOffset? OldestPendingWalAtUtc);

public sealed record BackupObservation(bool Enabled, BackupStatus? Status);
public interface IBackupStatusReader { Task<BackupStatus?> ReadAsync(CancellationToken ct); }

/// <summary>Backup health is independent of backend availability. Unknown cannot resolve stored incidents.</summary>
public static class BackupPolicy
{
    public static readonly IReadOnlySet<string> IncidentKeys = new HashSet<string>(StringComparer.Ordinal)
        { "backup_unavailable", "backup_failed", "backup_overdue", "backup_retention", "backup_archive" };

    public static void Evaluate(MonitorState state, BackupObservation? observation, DateTimeOffset now)
    {
        if (observation is not { Enabled: true }) return;
        var status = observation.Status;
        bool valid = Valid(status, now) && status!.Environment == state.Environment && status.Service == state.Service;
        MonitorPolicy.Update(state, "backup_unavailable", !valid, 1, "Backup status collection is unavailable, stale or invalid.", now);
        if (!valid)
        {
            foreach (var pair in state.Incidents.Where(p => IncidentKeys.Contains(p.Key) && p.Key != "backup_unavailable"))
            { pair.Value.BadSamples = 0; pair.Value.GoodSamples = 0; }
            return;
        }
        MonitorPolicy.Update(state, "backup_failed", !status!.RepositoryHealthy || status.FullBackupFailed || status.IncrementalBackupFailed,
            1, "Backup repository or a scheduled backup reported a failure.", now);
        var daily = status.LastIncrementalAtUtc > status.LastFullAtUtc ? status.LastIncrementalAtUtc : status.LastFullAtUtc;
        // A fresh full backup also satisfies the daily backup requirement.
        if (daily is null) daily = status.LastIncrementalAtUtc ?? status.LastFullAtUtc;
        bool overdue = status.LastFullAtUtc is null || now - status.LastFullAtUtc > TimeSpan.FromDays(8)
            || daily is null || now - daily > TimeSpan.FromHours(26);
        MonitorPolicy.Update(state, "backup_overdue", overdue, 1, "Backup full or daily success is missing or overdue.", now);
        MonitorPolicy.Update(state, "backup_retention", status.FullChains < 2, 1, "Backup repository has fewer than two successful full chains.", now);
        bool archive = !status.ArchiveConfigured
            || status.LastArchiveFailureAtUtc is { } failed && (status.LastArchivedAtUtc is null || failed > status.LastArchivedAtUtc)
            || status.PendingWalCount > 0 && now - status.OldestPendingWalAtUtc!.Value > TimeSpan.FromMinutes(2);
        MonitorPolicy.Update(state, "backup_archive", archive, 1, "WAL archiving is disabled, failed or has an overdue queue.", now);
    }

    public static bool Valid(BackupStatus? status, DateTimeOffset now)
    {
        if (status is null || status.ContractVersion != 1 || !status.CollectionSucceeded
            || status.ObservedAtUtc.Offset != TimeSpan.Zero || status.ObservedAtUtc > now.AddSeconds(30)
            || now - status.ObservedAtUtc > TimeSpan.FromMinutes(2) || status.FullChains is < 0 or > 1000000
            || status.PendingWalCount < 0 || (status.PendingWalCount == 0) != (status.OldestPendingWalAtUtc is null)
            || status.FullChains > 0 && status.LastFullAtUtc is null) return false;
        return new[] { status.LastFullAtUtc, status.LastIncrementalAtUtc, status.LastArchivedAtUtc,
            status.LastArchiveFailureAtUtc, status.OldestPendingWalAtUtc }.All(t => t is null || t.Value.Offset == TimeSpan.Zero && t <= status.ObservedAtUtc);
    }
}
