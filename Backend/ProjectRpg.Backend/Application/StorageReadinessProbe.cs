using System.Diagnostics;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Configuration;

namespace ProjectRpg.Backend.Application;

public sealed record StorageReadinessResult(
    bool IsReady,
    string Provider,
    long LatencyMilliseconds,
    string? FailureCode = null);

public interface IStorageReadinessProbe
{
    Task<StorageReadinessResult> CheckAsync(
        CancellationToken cancellationToken);
}

public sealed class MemoryStorageReadinessProbe : IStorageReadinessProbe
{
    public Task<StorageReadinessResult> CheckAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(new StorageReadinessResult(
            true,
            "Memory",
            0));
    }
}

public sealed class PostgresStorageReadinessProbe(
    NpgsqlDataSource dataSource,
    IOptions<StorageOptions> options,
    ILogger<PostgresStorageReadinessProbe> logger)
    : IStorageReadinessProbe
{
    private readonly object _gate = new();
    private Task<StorageReadinessResult>? _activeCheck;

    public async Task<StorageReadinessResult> CheckAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        long startedAt = Stopwatch.GetTimestamp();
        int timeoutSeconds = Math.Clamp(
            options.Value.ReadinessTimeoutSeconds,
            1,
            10);
        Task<StorageReadinessResult> check;
        lock (_gate)
        {
            // The driver may block while sending a PostgreSQL cancel request.
            // Keep at most one query/cleanup in flight across all HTTP probes.
            // A caller cancelling its request must not cancel other callers.
            if (_activeCheck is null || _activeCheck.IsCompleted)
            {
                _activeCheck = Task.Run(() => QueryAsync(timeoutSeconds));
            }
            check = _activeCheck;
        }

        try
        {
            return await check.WaitAsync(
                TimeSpan.FromSeconds(timeoutSeconds),
                cancellationToken);
        }
        catch (TimeoutException)
        {
            // QueryAsync owns disposal even when this HTTP response has ended.
            // Later probes reuse the pending task until its cleanup completes.
            return new StorageReadinessResult(
                false,
                "Postgres",
                GetElapsedMilliseconds(startedAt),
                "storage_timeout");
        }
    }

    private async Task<StorageReadinessResult> QueryAsync(int timeoutSeconds)
    {
        long startedAt = Stopwatch.GetTimestamp();
        using CancellationTokenSource timeout = new(
            TimeSpan.FromSeconds(timeoutSeconds));
        try
        {
            await using NpgsqlConnection connection =
                await dataSource.OpenConnectionAsync(timeout.Token);
            await using NpgsqlCommand command = new("SELECT 1;", connection)
            {
                CommandTimeout = timeoutSeconds
            };
            object? result = await command.ExecuteScalarAsync(timeout.Token);
            timeout.Token.ThrowIfCancellationRequested();
            bool isReady = result is int value && value == 1;
            if (!isReady)
            {
                logger.LogWarning(
                    "PostgreSQL readiness query returned an unexpected result.");
            }

            return new StorageReadinessResult(
                isReady,
                "Postgres",
                GetElapsedMilliseconds(startedAt),
                isReady ? null : "storage_unexpected_response");
        }
        catch (Exception exception) when (timeout.IsCancellationRequested
            || exception is TimeoutException
            || exception.InnerException is TimeoutException)
        {
            logger.LogWarning(
                "PostgreSQL readiness query timed out after {TimeoutSeconds} seconds.",
                timeoutSeconds);
            return new StorageReadinessResult(
                false,
                "Postgres",
                GetElapsedMilliseconds(startedAt),
                "storage_timeout");
        }
        catch (Exception exception)
        {
            logger.LogWarning(
                exception,
                "PostgreSQL readiness query failed.");
            return new StorageReadinessResult(
                false,
                "Postgres",
                GetElapsedMilliseconds(startedAt),
                "storage_unavailable");
        }
    }

    private static long GetElapsedMilliseconds(long startedAt) =>
        Math.Max(
            0,
            (long)Math.Ceiling(
                Stopwatch.GetElapsedTime(startedAt).TotalMilliseconds));
}
