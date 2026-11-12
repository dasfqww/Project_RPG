using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryAuctionRepository : IAuctionQueryRepository
{
    public Task<AuctionAssets?> AssetsAsync(Guid request, TradeParticipant actor, CancellationToken ct) => Read<AuctionAssets?>(() =>
    {
        if (!gate.AuctionReceipts.TryGetValue(request, out var r)) return null;
        if (AuctionQueryRules.ReceiptIdentity(new(r.Kind, r.Result)) != (request, actor)) return null;
        var context = Context(actor, ct);
        var records = items.FindByOwnerAsync(Owner(actor), false, AuctionAssets.MaximumItems + 1, ct).GetAwaiter().GetResult();
        var wallet = economy.LoadWalletAsync(context, ct).GetAwaiter().GetResult();
        return AuctionAssets.Create(request, actor, wallet, records, gate.UtcNow);
    }, ct);
    public Task<AuctionPage> SearchAsync(AuctionSearch q, string? sellerAccount, CancellationToken ct)
    {
        AuctionQueryRules.Validate(q);
        return Read(() =>
        {
            var now = gate.UtcNow;
            var found = gate.AuctionListings.Keys.Select(id => Load(id)!).Where(l => sellerAccount is not null
                    ? l.State.Seller.AccountId == sellerAccount : l.State.Status == AuctionListingStatus.Active && l.State.ExpiresAt > now)
                .Where(l => (q.DefinitionName is null || l.EscrowAtRegistration.DefinitionName == q.DefinitionName)
                    && (q.CurrencyCode is null || l.State.Terms.CurrencyCode == q.CurrencyCode)
                    && (q.MinimumPrice is null || l.State.TotalPrice >= q.MinimumPrice) && (q.MaximumPrice is null || l.State.TotalPrice <= q.MaximumPrice)
                    && AuctionQueryRules.After(l, q.After))
                .OrderBy(l => AuctionQueryRules.Cursor(l).ExpiresAt).ThenBy(l => l.State.ListingId).Take(q.Limit + 1).ToArray();
            return AuctionQueryRules.Page(found, q.Limit);
        }, ct);
    }
    public Task<AuctionEscrowListing?> FindVisibleAsync(Guid id, string account, CancellationToken ct) => Read(() =>
    {
        var l = Load(id); return l is not null && (l.State.Seller.AccountId == account || l.State.Status == AuctionListingStatus.Active && l.State.ExpiresAt > gate.UtcNow) ? l : null;
    }, ct);
    public Task<AuctionStoredReceipt?> FindReceiptAsync(Guid id, TradeParticipant actor, CancellationToken ct) => Read(() =>
    {
        if (!gate.AuctionReceipts.TryGetValue(id, out var r)) return null;
        var receipt = new AuctionStoredReceipt(r.Kind, r.Result);
        var identity = AuctionQueryRules.ReceiptIdentity(receipt);
        if (identity.RequestId != id) throw new InvalidDataException("Auction receipt ID mismatch.");
        return identity.Actor == actor ? receipt : null;
    }, ct);
    public Task<IReadOnlyList<AuctionEscrowListing>> FindExpiredAsync(int limit, AuctionCursor? after, CancellationToken ct)
    {
        AuctionQueryRules.Validate(new(limit, After: after));
        return Read<IReadOnlyList<AuctionEscrowListing>>(() =>
        {
            var now = gate.UtcNow; return gate.AuctionListings.Keys.Select(id => Load(id)!).Where(l => l.State.Status == AuctionListingStatus.Active
                && l.State.ExpiresAt <= now && AuctionQueryRules.After(l, after)).OrderBy(l => AuctionQueryRules.Cursor(l).ExpiresAt).ThenBy(l => l.State.ListingId).Take(limit).ToArray();
        }, ct);
    }
}
