using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public interface IAuctionPurchaseRepository
{
    Task<AuctionPurchaseResult> PurchaseAsync(AuctionPurchaseCommand command, CancellationToken ct);
}
