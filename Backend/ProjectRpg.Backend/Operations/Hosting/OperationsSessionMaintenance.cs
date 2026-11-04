using ProjectRpg.Backend.Operations.Infrastructure.Authentication;

namespace ProjectRpg.Backend.Operations.Hosting;

internal sealed class OperationsSessionMaintenance(IOperatorSessionRepository sessions, TimeProvider clock,
    ILogger<OperationsSessionMaintenance> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using PeriodicTimer timer = new(TimeSpan.FromMinutes(15), clock);
        do
        {
            try { await sessions.PurgeAsync(clock.GetUtcNow(), stoppingToken); }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { return; }
            catch (Exception error) when (error is Npgsql.NpgsqlException or TimeoutException)
            { logger.LogWarning("Operations session cleanup unavailable."); }
        } while (await timer.WaitForNextTickAsync(stoppingToken));
    }
}
