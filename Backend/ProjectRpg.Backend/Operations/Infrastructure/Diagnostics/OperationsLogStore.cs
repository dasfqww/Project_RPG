using System.Text.Json;
using Microsoft.Extensions.Logging;
using ProjectRpg.Backend.Operations.Hosting;
using Serilog;
using Serilog.Extensions.Logging;
using Serilog.Formatting.Json;
using Serilog.Sinks.Async;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsLogStore : IHostedService, IDisposable, IAsyncLogEventSinkMonitor
{
    private readonly OperationsOptions options;
    private readonly TimeProvider clock;
    private readonly Serilog.Core.Logger sink;
    private readonly SerilogLoggerFactory factory;
    private readonly object gate = new();
    private IAsyncLogEventSinkInspector? inspector;
    private long errors;
    private int disposed;
    private readonly DateTimeOffset coverageStart;
    private LogFileSnapshot[] previousFiles = [];
    public string BootId { get; } = Guid.NewGuid().ToString("N");
    public string Root => options.LogRoot;
    public long Dropped => inspector?.DroppedMessagesCount ?? 0;
    public long SinkErrors => Interlocked.Read(ref errors);
    public DateTimeOffset CoverageStart => coverageStart;

    public OperationsLogStore(OperationsOptions options, TimeProvider clock)
    {
        this.options = options; this.clock = clock;
        ManagedPath.RejectLinks(Root);
        Directory.CreateDirectory(Root);
        coverageStart = clock.GetUtcNow();
        string cataloguePath = Path.Combine(Root, "catalogue.json");
        try
        {
            if (File.Exists(cataloguePath) && (File.GetAttributes(cataloguePath) & FileAttributes.ReparsePoint) == 0
                && new FileInfo(cataloguePath).Length <= 1024 * 1024)
            {
                Catalogue? old = JsonSerializer.Deserialize<Catalogue>(File.ReadAllText(cataloguePath));
                if (old is { CleanStop: true, Dropped: 0, SinkErrors: 0 } && old.Environment == options.EnvironmentId
                    && old.Source == options.SourceId && old.Files.All(f => Files().Any(current => current.Name == f.Name
                        && current.Length >= f.Length && current.CreationTimeUtc == f.CreatedUtc)))
                    coverageStart = old.CoverageStart;
            }
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException) { Interlocked.Increment(ref errors); }
        Serilog.Debugging.SelfLog.Enable(_ => Interlocked.Increment(ref errors));
        sink = new LoggerConfiguration().MinimumLevel.Information().WriteTo.Async(
            write => write.File(new JsonFormatter(renderMessage: false), Path.Combine(Root, "ops-.jsonl"),
                rollingInterval: RollingInterval.Day, fileSizeLimitBytes: 32L * 1024 * 1024,
                rollOnFileSizeLimit: true, retainedFileCountLimit: 30, retainedFileTimeLimit: TimeSpan.FromDays(14),
                shared: false), bufferSize: 4096, blockWhenFull: false, monitor: this).CreateLogger();
        factory = new SerilogLoggerFactory(sink, dispose: false);
        SaveCatalogue(false);
    }
    public Microsoft.Extensions.Logging.ILogger CreateLogger() => factory.CreateLogger("Operations");
    public void StartMonitoring(IAsyncLogEventSinkInspector value) => inspector = value;
    public void StopMonitoring(IAsyncLogEventSinkInspector value) { }
    public Task StartAsync(CancellationToken ct) => Task.CompletedTask;
    public async Task StopAsync(CancellationToken ct)
    {
        try { await Task.Run(DisposeSink, CancellationToken.None).WaitAsync(TimeSpan.FromSeconds(5), ct); SaveCatalogue(true); }
        catch (Exception e) when (e is TimeoutException or OperationCanceledException) { Interlocked.Increment(ref errors); SaveCatalogue(false); }
    }
    public void Dispose() { DisposeSink(); factory.Dispose(); }
    private void DisposeSink() { if (Interlocked.Exchange(ref disposed, 1) == 0) sink.Dispose(); }

    internal sealed record LogFileSnapshot(string Name, long Length, DateTime CreationTimeUtc, DateTime LastWriteTimeUtc);
    internal LogFileSnapshot[] Files()
    {
        ManagedPath.RejectLinks(Root);
        FileInfo[] files = new DirectoryInfo(Root).GetFiles("ops-*.jsonl")
            .OrderByDescending(f => f.Name, StringComparer.Ordinal).ToArray();
        if (files.Length > 30 || files.Any(f => (f.Attributes & FileAttributes.ReparsePoint) != 0 || f.Name.Length > 64))
            throw new IOException("Invalid or excessive operations log sources.");
        // Directory enumeration may report a stale length while a writer holds the
        // file open on Windows. Capture EOF from a shared read handle instead.
        return files.Select(f =>
        {
            using FileStream stream = new(f.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            return new LogFileSnapshot(f.Name, stream.Length, f.CreationTimeUtc, f.LastWriteTimeUtc);
        }).ToArray();
    }
    internal void SaveCatalogue(bool clean)
    {
        lock (gate)
        {
            string temp = Path.Combine(Root, "catalogue." + BootId + ".tmp");
            try
            {
                var current = Files();
                if (previousFiles.Any(old => !current.Any(f => f.Name == old.Name && f.Length >= old.Length && f.CreationTimeUtc == old.CreationTimeUtc)))
                    Interlocked.Increment(ref errors); // Retention/deletion cannot produce a false Complete history.
                previousFiles = current;
                var files = current.Select(f => new FileEntry(f.Name, f.Length, f.CreationTimeUtc, f.LastWriteTimeUtc)).ToArray();
                string json = JsonSerializer.Serialize(new Catalogue(1, options.EnvironmentId, options.SourceId, BootId,
                    coverageStart, clock.GetUtcNow(), clean, Dropped, SinkErrors, files));
                File.WriteAllText(temp, json);
                File.Move(temp, Path.Combine(Root, "catalogue.json"), overwrite: true);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { Interlocked.Increment(ref errors); }
            finally
            {
                try { if (File.Exists(temp)) File.Delete(temp); }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException) { Interlocked.Increment(ref errors); }
            }
        }
    }
    private sealed record FileEntry(string Name, long Length, DateTime CreatedUtc, DateTime LastWriteUtc);
    private sealed record Catalogue(int Version, string Environment, string Source, string Boot, DateTimeOffset CoverageStart,
        DateTimeOffset ObservedAt, bool CleanStop, long Dropped, long SinkErrors, FileEntry[] Files);
}
