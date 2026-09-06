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
    public async Task<StorageReadinessResult> CheckAsync(
        CancellationToken cancellationToken)
    {
        long startedAt = Stopwatch.GetTimestamp();
        int timeoutSeconds = Math.Clamp(
            options.Value.ReadinessTimeoutSeconds,
            1,
            10);
        using CancellationTokenSource timeout =
            CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(timeoutSeconds));

        try
        {
            await using NpgsqlConnection connection =
                await dataSource.OpenConnectionAsync(timeout.Token);
            await using NpgsqlCommand command = new("SELECT 1;", connection)
            {
                CommandTimeout = timeoutSeconds
            };
            object? result = await command.ExecuteScalarAsync(timeout.Token);
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
        catch (OperationCanceledException)
            when (!cancellationToken.IsCancellationRequested)
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
