using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Application;

public sealed class AuctionExpiryWorker(IAuctionQueryRepository queries, IAuctionEscrowRepository escrow, ILogger<AuctionExpiryWorker> logger) : BackgroundService
{
    private AuctionCursor? after;
    private readonly SemaphoreSlim sweep = new(1, 1);
    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try { await SweepAsync(ct); }
            catch (OperationCanceledException) when (ct.IsCancellationRequested) { break; }
            catch (Exception e) { logger.LogError(e, "Auction expiry scan failed; retrying later."); }
            try { await Task.Delay(TimeSpan.FromSeconds(5), ct); } catch (OperationCanceledException) when (ct.IsCancellationRequested) { break; }
        }
    }
    public async Task<int> SweepAsync(CancellationToken ct)
    {
        await sweep.WaitAsync(ct);
        try
        {
            var due = await queries.FindExpiredAsync(50, after, ct); int closed = 0;
            foreach (var l in due)
            {
                try
                {
                    // Fresh worker-only IDs cannot be preoccupied through a predictable player request.
                    await escrow.ExecuteAsync(new(Guid.NewGuid(), l.State.ListingId, l.State.Seller, AuctionEscrowKind.Expire, l.State.Revision), ct); closed++;
                }
                catch (TradeRuleException e) when (e.Code is "auction_revision_conflict" or "auction_not_active" or "auction_not_expired") { }
                catch (OperationCanceledException) when (ct.IsCancellationRequested) { throw; }
                catch (Exception e) { logger.LogWarning(e, "Auction {ListingId} return pending; retrying later.", l.State.ListingId); }
                after = AuctionQueryRules.Cursor(l);
            }
            // A blocked old return does not starve later listings; restart the scan after reaching its end.
            if (due.Count < 50) after = null;
            return closed;
        }
        finally { sweep.Release(); }
    }
}
