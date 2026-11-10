using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public interface IAuctionEscrowRepository
{
    Task<AuctionEscrowListing?> FindAsync(Guid listingId, TradeParticipant seller, CancellationToken ct);
    Task<AuctionEscrowResult> ExecuteAsync(AuctionEscrowCommand command, CancellationToken ct);
}
