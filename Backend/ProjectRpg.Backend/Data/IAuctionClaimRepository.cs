using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public interface IAuctionClaimRepository
{
    Task<AuctionSellerPayable?> FindPayableAsync(Guid listingId, TradeParticipant claimant, CancellationToken ct);
    Task<AuctionClaimResult> ClaimAsync(AuctionClaimCommand command, CancellationToken ct);
}
