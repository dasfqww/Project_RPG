using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Threading.RateLimiting;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Operations.Core.Monitoring;

namespace ProjectRpg.Backend.Operations.Monitoring;

public sealed class OperationsMonitorOptions
{
    public bool Enabled { get; set; }
    public string Token { get; set; } = "";
    public void Validate()
    {
        if (Enabled && (Token.Length is < 32 or > 256 || Token.Any(c => !char.IsAsciiLetterOrDigit(c) && c is not '-' and not '_')))
            throw new InvalidOperationException("Operations monitor requires a separate 32-256 character secret token.");
    }
}
public sealed class OperationsMonitorLimits : IDisposable
{
    // Single configured identity; a spoofed token cannot allocate arbitrary limiter partitions.
    private readonly FixedWindowRateLimiter limiter = new(new() { PermitLimit=30, Window=TimeSpan.FromMinutes(1), QueueLimit=0, AutoReplenishment=true });
    public RateLimitLease Acquire() => limiter.AttemptAcquire();
    public void Dispose() => limiter.Dispose();
}
public sealed class OperationsMonitorBoundary(RequestDelegate next)
{
    public async Task InvokeAsync(HttpContext context, OperationsOptions options, OperationsMonitorLimits limits)
    {
        if (!context.Request.Path.StartsWithSegments("/internal/operations")) { await next(context); return; }
        context.Response.Headers.CacheControl = "no-store";
        if (!options.Monitor.Enabled || context.Request.Path != "/internal/operations/status" || !HttpMethods.IsGet(context.Request.Method))
        { context.Response.StatusCode=404; return; }
        IPAddress? address=context.Connection.RemoteIpAddress;
        if (address?.IsIPv4MappedToIPv6==true) address=address.MapToIPv4();
        var origin=new Uri(options.ManagementOrigin);
        if (!context.Request.IsHttps || !string.Equals(context.Request.Host.Value,origin.Authority,StringComparison.OrdinalIgnoreCase)
            || address is null || !options.AllowedNetworks.Any(n=>System.Net.IPNetwork.Parse(n).Contains(address)))
        { context.Response.StatusCode=403; return; }
        string authorization=context.Request.Headers.Authorization.ToString();
        if (!authorization.StartsWith("Bearer ",StringComparison.Ordinal) || authorization.Length>263
            || !CryptographicOperations.FixedTimeEquals(SHA256.HashData(Encoding.UTF8.GetBytes(authorization[7..])),
                SHA256.HashData(Encoding.UTF8.GetBytes(options.Monitor.Token))))
        { context.Response.StatusCode=401; return; }
        using var lease=limits.Acquire();
        if (!lease.IsAcquired) { context.Response.Headers.RetryAfter="60"; context.Response.StatusCode=429; return; }
        if (context.Request.QueryString.HasValue) { context.Response.StatusCode=400; return; }
        using var deadline=CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
        deadline.CancelAfter(TimeSpan.FromSeconds(5));
        var original=context.RequestAborted; context.RequestAborted=deadline.Token;
        try { await next(context); }
        catch (Exception e) when (e is NpgsqlException or TimeoutException or OperationCanceledException or IOException or UnauthorizedAccessException)
        {
            if (!original.IsCancellationRequested && !context.Response.HasStarted)
            { context.Response.StatusCode=503; await context.Response.WriteAsJsonAsync(new { status="Unknown" }, CancellationToken.None); }
        }
        finally { context.RequestAborted=original; }
    }
}
public static class OperationsMonitoring
{
    public static void Register(IServiceCollection services, bool postgres)
    {
        services.AddSingleton<OperationsMonitorLimits>();
        if (postgres) services.AddSingleton<ISettlementHealthReader, PostgresSettlementHealthReader>();
        else services.AddSingleton<ISettlementHealthReader, MemorySettlementHealthReader>();
    }
    public static void Map(WebApplication app)
    {
        if (!app.Services.GetRequiredService<OperationsOptions>().Monitor.Enabled) return;
        app.MapGet("/internal/operations/status", async (HttpContext context, ISettlementHealthReader settlements, OperationsLogStore logs, TimeProvider clock) =>
        {
            var health=await settlements.ReadAsync(context.RequestAborted);
            var files=logs.Files();
            DateTimeOffset? last=files.Length==0 ? null : new(files.Max(f=>f.LastWriteTimeUtc),TimeSpan.Zero);
            return Results.Json(new RuntimeStatus(clock.GetUtcNow(),logs.BootId,true,health,
                new(logs.Dropped,logs.SinkErrors,files.Sum(f=>f.Length),last)), MonitoringJson.Options);
        });
    }
}
public sealed class MemorySettlementHealthReader(InMemoryGameRepository game) : ISettlementHealthReader
{
    public Task<SettlementHealth> ReadAsync(CancellationToken ct) { ct.ThrowIfCancellationRequested(); return Task.FromResult(game.ReadOperationsSettlementHealth()); }
}
public sealed class PostgresSettlementHealthReader(NpgsqlDataSource source) : ISettlementHealthReader
{
    public async Task<SettlementHealth> ReadAsync(CancellationToken ct)
    {
        await using var connection=await source.OpenConnectionAsync(ct);
        await using var transaction=await connection.BeginTransactionAsync(System.Data.IsolationLevel.RepeatableRead,ct);
        await using (NpgsqlCommand mode=new("SET TRANSACTION READ ONLY",connection,transaction)) await mode.ExecuteNonQueryAsync(ct);
        await using NpgsqlCommand command=new("""
            SELECT count(*) FILTER (WHERE state='Pending'), count(*) FILTER (WHERE state='Processing'), count(*) FILTER (WHERE state='Failed'),
                min(created_at) FILTER (WHERE state IN ('Pending','Processing')), max(updated_at) FILTER (WHERE state='Failed')
            FROM dungeon_reward_settlements
            """,connection,transaction) { CommandTimeout=3 };
        await using var row=await command.ExecuteReaderAsync(ct); await row.ReadAsync(ct);
        return new(row.GetInt64(0),row.GetInt64(1),row.GetInt64(2), row.IsDBNull(3)?null:row.GetFieldValue<DateTimeOffset>(3),
            row.IsDBNull(4)?null:row.GetFieldValue<DateTimeOffset>(4));
    }
}
