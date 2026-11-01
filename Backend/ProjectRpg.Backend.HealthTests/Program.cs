using System.Diagnostics;
using System.Net;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.HealthTests;

return await RunAsync();

static async Task<int> RunAsync()
{
    try
    {
        NpgsqlConnectionStringBuilder target = new(
            Environment.GetEnvironmentVariable("POSTGRES_CONNECTION_STRING")
            ?? "Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only");
        Require(target.Host is "127.0.0.1" or "localhost" or "::1",
            "Health fault tests require a local development PostgreSQL instance.");
        await using FaultProxy proxy = new(target.Host!, target.Port);
        proxy.Start();
        target.Host = "127.0.0.1";
        target.Port = proxy.Port;
        target.MaxPoolSize = 1;
        target.MinPoolSize = 0;
        target.Timeout = 15;
        await using NpgsqlDataSource dataSource = NpgsqlDataSource.Create(target.ConnectionString);

        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        // Do not inherit the normal backend's ports, providers or log settings.
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(dataSource);
        builder.Services.AddSingleton<IOptions<StorageOptions>>(
            Options.Create(new StorageOptions { ReadinessTimeoutSeconds = 1 }));
        builder.Services.AddSingleton<IStorageReadinessProbe, PostgresStorageReadinessProbe>();
        await using WebApplication app = builder.Build();
        app.MapHealthApi();
        await app.StartAsync();
        using HttpClient client = new()
        {
            BaseAddress = new Uri(app.Urls.Single()),
            Timeout = TimeSpan.FromSeconds(8)
        };
        IStorageReadinessProbe probe = app.Services.GetRequiredService<IStorageReadinessProbe>();

        try
        {
            await ExpectHealthAsync(client, "/health/ready", HttpStatusCode.OK, "ready");
            await ExpectLivenessAsync(client);
            Console.WriteLine("PASS PostgreSQL readiness and liveness; responses cannot be cached.");

            proxy.SetMode(ProxyMode.Disconnected);
            await ExpectHealthAsync(client, "/health/ready", HttpStatusCode.ServiceUnavailable,
                "not_ready", "storage_unavailable");
            await ExpectLivenessAsync(client);
            await RecoverAsync(proxy, client);
            Console.WriteLine("PASS connection loss returns 503; liveness stays 200; recovery needs no restart.");

            // Keep the warmed pooled connection open and drop the actual SELECT 1.
            proxy.SetMode(ProxyMode.Stalled, disconnectExisting: false);
            await Task.WhenAll(Enumerable.Range(0, 8).Select(_ =>
                ExpectHealthAsync(client, "/health/ready", HttpStatusCode.ServiceUnavailable,
                    "not_ready", "storage_timeout")));
            await ExpectLivenessAsync(client);
            await RecoverAsync(proxy, client);
            Console.WriteLine("PASS concurrent stalled-query probes have bounded timeouts and recover.");

            proxy.SetMode(ProxyMode.Stalled);
            dataSource.Clear();
            await ExpectHealthAsync(client, "/health/ready", HttpStatusCode.ServiceUnavailable,
                "not_ready", "storage_timeout");
            await RecoverAsync(proxy, client);
            Console.WriteLine("PASS stalled connection handshake has a bounded timeout and recovers.");

            // A healthy DB is not enough if every application connection is leased.
            await using (NpgsqlConnection heldConnection = await dataSource.OpenConnectionAsync())
            {
                await ExpectHealthAsync(client, "/health/ready", HttpStatusCode.ServiceUnavailable,
                    "not_ready", "storage_timeout");
                await ExpectLivenessAsync(client);
            }
            await RecoverAsync(proxy, client);
            Console.WriteLine("PASS exhausted application pool reports not_ready and recovers after release.");

            using (CancellationTokenSource cancelled = new())
            {
                cancelled.Cancel();
                await ExpectCancellationAsync(probe, cancelled.Token);
            }
            proxy.SetMode(ProxyMode.Stalled);
            dataSource.Clear();
            using (CancellationTokenSource cancelled = new(TimeSpan.FromMilliseconds(150)))
            {
                await ExpectCancellationAsync(probe, cancelled.Token);
            }
            await RecoverAsync(proxy, client);
            Console.WriteLine("PASS caller cancellation propagates without becoming a storage outage.");
        }
        finally
        {
            proxy.SetMode(ProxyMode.Forwarding);
            await app.StopAsync();
        }
        Console.WriteLine("All health fault tests passed. PostgreSQL was not stopped or modified.");
        return 0;
    }
    catch (Exception exception)
    {
        // Never print connection strings or raw database exceptions.
        Console.Error.WriteLine($"FAIL {exception.GetType().Name}: {exception.Message}");
        return 1;
    }
}

static async Task ExpectCancellationAsync(IStorageReadinessProbe probe, CancellationToken token)
{
    try
    {
        await probe.CheckAsync(token);
    }
    catch (OperationCanceledException exception) when (exception.CancellationToken == token)
    {
        return;
    }
    throw new InvalidOperationException("Caller cancellation was swallowed by the readiness probe.");
}

static async Task RecoverAsync(FaultProxy proxy, HttpClient client)
{
    proxy.SetMode(ProxyMode.Forwarding);
    // Allow the previous query's cancellation and pooled socket cleanup to finish.
    long startedAt = Stopwatch.GetTimestamp();
    while (Stopwatch.GetElapsedTime(startedAt) < TimeSpan.FromSeconds(6))
    {
        using HttpResponseMessage response = await client.GetAsync("/health/ready");
        if (response.StatusCode == HttpStatusCode.OK)
        {
            await ExpectHealthAsync(client, "/health/ready", HttpStatusCode.OK, "ready");
            return;
        }
        await Task.Delay(100);
    }
    throw new InvalidOperationException("Readiness did not recover after restoring the test connection.");
}

static async Task ExpectLivenessAsync(HttpClient client)
{
    await ExpectHealthAsync(client, "/health", HttpStatusCode.OK, "ok");
    await ExpectHealthAsync(client, "/health/live", HttpStatusCode.OK, "ok");
}

static async Task ExpectHealthAsync(
    HttpClient client, string path, HttpStatusCode expectedCode,
    string expectedStatus, string? expectedFailure = null)
{
    Stopwatch elapsed = Stopwatch.StartNew();
    using HttpResponseMessage response = await client.GetAsync(path);
    string body = await response.Content.ReadAsStringAsync();
    Require(response.StatusCode == expectedCode,
        $"{path}: expected {(int)expectedCode}, got {(int)response.StatusCode}.");
    Require(response.Headers.CacheControl?.NoStore == true,
        $"{path}: health response may be cached.");
    using JsonDocument json = JsonDocument.Parse(body);
    Require(json.RootElement.GetProperty("status").GetString() == expectedStatus,
        $"{path}: unexpected status.");
    if (path == "/health/ready")
    {
        Require(json.RootElement.GetProperty("storageProvider").GetString() == "Postgres",
            "Readiness did not check PostgreSQL.");
        string? failure = json.RootElement.GetProperty("failureCode").GetString();
        Require(failure == expectedFailure,
            $"{path}: expected failure '{expectedFailure}', got '{failure}'.");
        Require(json.RootElement.EnumerateObject().Count() == 4,
            "Readiness response exposed unexpected diagnostic fields.");
        Require(json.RootElement.GetProperty("storageLatencyMilliseconds").GetInt64() >= 0,
            "Readiness latency was negative.");
    }
    // One-second probe budget plus local scheduling allowance, not driver timeout.
    Require(elapsed.Elapsed < TimeSpan.FromSeconds(2.5),
        $"{path}: exceeded the HTTP readiness deadline ({elapsed.Elapsed.TotalSeconds:F2}s).");
}

static void Require(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}
