using ProjectRpg.Backend.Data;
using Microsoft.Extensions.DependencyInjection.Extensions;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.ServiceControl;

namespace ProjectRpg.Backend.Operations.ServiceControl;

internal static class ServiceControlHosting
{
    internal static void Register(IServiceCollection services, IConfiguration config)
    {
        // State/gates remain enforced even if the operator console is disabled.
        services.TryAddSingleton<TimeProvider>(TimeProvider.System);
        if (string.Equals(config["Storage:Provider"], "Postgres", StringComparison.OrdinalIgnoreCase))
        {
            services.AddSingleton<PostgresServiceControlStore>();
            services.AddSingleton<IServiceControlReader>(p => p.GetRequiredService<PostgresServiceControlStore>());
            services.AddSingleton<IServiceControlCommitter>(p => p.GetRequiredService<PostgresServiceControlStore>());
        }
        else
        {
            services.AddSingleton(p => new MemoryServiceControlStore((InMemoryGameRepository)p.GetRequiredService<IGameRepository>(), p.GetRequiredService<TimeProvider>()));
            services.AddSingleton<IServiceControlReader>(p => p.GetRequiredService<MemoryServiceControlStore>());
            services.AddSingleton<IServiceControlCommitter>(p => p.GetRequiredService<MemoryServiceControlStore>());
        }
        services.AddSingleton<PublicServiceStatusLimits>();
    }
    internal static void MapPublic(WebApplication app)
    {
        app.MapGet("/api/service-status", async (HttpContext c, IServiceControlReader reader, TimeProvider clock, PublicServiceStatusLimits limits) =>
        {
            c.Response.Headers.CacheControl = "no-store";
            if (!limits.Allow(c.Connection.RemoteIpAddress?.ToString() ?? "unknown", clock.GetUtcNow()))
            { c.Response.Headers.RetryAfter = "60"; return Results.StatusCode(429); }
            try
            {
                var result = await reader.ReadAsync(c.RequestAborted);
                return result.IsSuccess ? Results.Json(new { contractVersion = 1, data = PublicServiceStatus.From(result.Value, clock.GetUtcNow()) }) : Results.StatusCode(503);
            }
            catch (Exception e) when (e is Npgsql.NpgsqlException or TimeoutException)
            { return Results.StatusCode(503); }
        }).AllowAnonymous();
    }
}

internal sealed class PublicServiceStatusLimits
{
    private readonly Dictionary<string, (DateTimeOffset Start, int Count)> _clients = new(StringComparer.Ordinal);
    internal bool Allow(string ip, DateTimeOffset now)
    {
        lock (_clients)
        {
            foreach (var key in _clients.Where(p => now - p.Value.Start >= TimeSpan.FromMinutes(1)).Select(p => p.Key).ToArray()) _clients.Remove(key);
            if (!_clients.TryGetValue(ip, out var window))
            { if (_clients.Count >= 4096) return false; window = (now, 0); }
            _clients[ip] = (window.Start, window.Count + 1);
            return window.Count < 60;
        }
    }
}
