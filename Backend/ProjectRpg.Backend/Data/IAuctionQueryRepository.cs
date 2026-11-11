using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public interface IAuctionQueryRepository
{
    // sellerAccount is trusted application identity, never a player query parameter.
    Task<AuctionPage> SearchAsync(AuctionSearch query, string? sellerAccount, CancellationToken ct);
    Task<AuctionEscrowListing?> FindVisibleAsync(Guid id, string account, CancellationToken ct);
    Task<AuctionStoredReceipt?> FindReceiptAsync(Guid id, TradeParticipant actor, CancellationToken ct);
    Task<IReadOnlyList<AuctionEscrowListing>> FindExpiredAsync(int limit, AuctionCursor? after, CancellationToken ct);
}
