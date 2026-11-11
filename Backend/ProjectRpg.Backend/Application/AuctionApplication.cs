using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Application;

public sealed class AuctionApplication(IGameRepository game, IAuctionEscrowRepository escrow, IAuctionPurchaseRepository purchases,
    IAuctionClaimRepository claims, IAuctionQueryRepository queries, IOptions<AuctionOptions> options, AuctionRequestLimiter limiter)
{
    public async Task<TradeParticipant> ActorAsync(AuthenticatedPrincipal principal, Guid id, bool search, CancellationToken ct)
    {
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null || principal.IsSecurityTelemetryOnly) throw new TradeRuleException("auction_player_required");
        var c = await game.GetCharacterEconomyContextAsync(id, ct);
        if (c?.SteamId != principal.SteamId) throw new TradeRuleException("character_not_owned");
        if (!limiter.Acquire(principal.SteamId, search)) throw new TradeRuleException("auction_rate_limit");
        return new(principal.SteamId, id);
    }
    public Task<AuctionPage> SearchAsync(AuctionSearch q, TradeParticipant actor, bool mine, CancellationToken ct)
    { if (!mine) Enabled(); return queries.SearchAsync(q, mine ? actor.AccountId : null, ct); }
    public async Task<AuctionEscrowListing?> FindAsync(Guid id, TradeParticipant actor, CancellationToken ct)
    {
        var l = await queries.FindVisibleAsync(id, actor.AccountId, ct);
        return l is not null && (options.Value.Enabled || l.State.Seller.AccountId == actor.AccountId) ? l : null;
    }
    public async Task<AuctionEscrowResult> RegisterAsync(TradeParticipant actor, Guid id, Guid request, TradeOfferedItem item, long price, DateTimeOffset expires, CancellationToken ct)
    {
        Enabled();
        // Keep server terms stable when replaying a registration after a policy change.
        var previous = await escrow.FindAsync(id, actor, ct);
        return await escrow.ExecuteAsync(new(request, id, actor, AuctionEscrowKind.Register, Item: item, TotalPrice: price, ExpiresAt: expires,
            Terms: previous?.State.Terms ?? options.Value.Terms()), ct);
    }
    public Task<AuctionPurchaseResult> PurchaseAsync(AuctionPurchaseCommand cmd, CancellationToken ct) { Enabled(); return purchases.PurchaseAsync(cmd, ct); }
    public Task<AuctionEscrowResult> CancelAsync(TradeParticipant actor, Guid id, Guid request, long revision, CancellationToken ct) => escrow.ExecuteAsync(new(request, id, actor, AuctionEscrowKind.Cancel, revision), ct);
    public Task<AuctionSellerPayable?> PayableAsync(Guid id, TradeParticipant actor, CancellationToken ct) => claims.FindPayableAsync(id, actor, ct);
    public Task<AuctionClaimResult> ClaimAsync(AuctionClaimCommand cmd, CancellationToken ct) => claims.ClaimAsync(cmd, ct);
    public Task<AuctionStoredReceipt?> ReceiptAsync(Guid id, TradeParticipant actor, CancellationToken ct) => queries.FindReceiptAsync(id, actor, ct);
    private void Enabled() { if (!options.Value.Enabled) throw new TradeRuleException("auctions_disabled"); }
}
