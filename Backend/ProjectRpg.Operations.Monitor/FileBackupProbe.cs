using System.Text.Json;
using System.Text.Json.Serialization;
using ProjectRpg.Operations.Core.Monitoring;

namespace ProjectRpg.Operations.Monitor;

public sealed class FileBackupStatusReader(string path) : IBackupStatusReader
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
        { RespectRequiredConstructorParameters = true, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow };

    public async Task<BackupStatus?> ReadAsync(CancellationToken ct)
    {
        try
        {
            if (!Path.IsPathFullyQualified(path)) return null;
            for (string? current = Path.GetFullPath(path); current is not null; current = Path.GetDirectoryName(current))
                if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) return null;
            await using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete,
                bufferSize: 4096, useAsync: true);
            byte[] bytes = new byte[8193]; int total = 0, read;
            while (total < bytes.Length && (read = await stream.ReadAsync(bytes.AsMemory(total), ct)) > 0) total += read;
            return total == bytes.Length ? null : JsonSerializer.Deserialize<BackupStatus>(bytes.AsSpan(0, total), Json);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or NotSupportedException)
        { return null; }
    }
}

public sealed class BackupAwareProbe(IMonitorProbe runtime, IBackupStatusReader backup) : IMonitorProbe
{
    public async Task<MonitorObservation> ObserveAsync(CancellationToken ct)
    {
        var backend = runtime.ObserveAsync(ct);
        var status = backup.ReadAsync(ct);
        await Task.WhenAll(backend, status);
        return backend.Result with { Backup = new(true, status.Result) };
    }
}
