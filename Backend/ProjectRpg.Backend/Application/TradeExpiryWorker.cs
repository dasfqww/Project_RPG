using System.Security.Cryptography;
using System.Text;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Application;

public sealed class TradeExpiryWorker(ITradeSessionRepository store, ILogger<TradeExpiryWorker> logger) : BackgroundService
{
    // Keep running when new trading is disabled: outstanding reservations still need closing.
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            try { await SweepAsync(stoppingToken); }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { break; }
            catch (Exception error) { logger.LogError(error, "Trade expiry scan failed; retrying on next sweep."); }
            try { await Task.Delay(TimeSpan.FromSeconds(5), stoppingToken); }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { break; }
        }
    }

    public async Task<int> SweepAsync(CancellationToken ct)
    {
        int closed = 0;
        foreach (var session in await store.FindExpiredAsync(100, ct))
        {
            var state = session.State;
            // Repeated workers/processes generate the same receipt key for the same observed revision.
            var id = new Guid(SHA256.HashData(Encoding.UTF8.GetBytes($"trade_expire|{state.TradeId:D}|{state.Revision}" )).AsSpan(0, 16));
            try
            {
                await store.ExecuteAsync(new(id, state.TradeId, state.Initiator, TradeCommandKind.Expire, state.Revision), ct);
                closed++;
            }
            catch (TradeRuleException e) when (e.Code is "trade_revision_conflict" or "trade_closed" or "trade_not_expired") { }
            catch (OperationCanceledException) when (ct.IsCancellationRequested) { throw; }
            catch (Exception error) { logger.LogError(error, "Trade {TradeId} expiry failed; retrying later.", state.TradeId); }
        }
        return closed;
    }
}
