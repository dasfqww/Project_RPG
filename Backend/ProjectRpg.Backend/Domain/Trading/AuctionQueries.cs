using System.Text.Json;

namespace ProjectRpg.Backend.Domain.Trading;

public sealed record AuctionCursor(DateTimeOffset ExpiresAt, Guid ListingId);
public sealed record AuctionSearch(int Limit = 20, string? DefinitionName = null, string? CurrencyCode = null,
    long? MinimumPrice = null, long? MaximumPrice = null, AuctionCursor? After = null);
public sealed record AuctionPage(IReadOnlyList<AuctionEscrowListing> Listings, AuctionCursor? Next);
public sealed record AuctionStoredReceipt(string Kind, string Json);

internal static class AuctionQueryRules
{
    internal static void Validate(AuctionSearch q)
    {
        if (q is null || q.Limit is < 1 or > 50
            || q.DefinitionName is { } name && (string.IsNullOrWhiteSpace(name) || name.Length > 128 || name.Any(char.IsControl))
            || q.CurrencyCode is { } code && (string.IsNullOrWhiteSpace(code) || code.Length > 64 || !code.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-'))
            || q.MinimumPrice is <= 0 || q.MaximumPrice is <= 0 || q.MinimumPrice > q.MaximumPrice
            || q.After is { } after && (after.ListingId == Guid.Empty || after.ExpiresAt.Offset != TimeSpan.Zero || after.ExpiresAt.UtcTicks % 10 != 0))
            throw new TradeRuleException("invalid_auction_query");
    }
    internal static AuctionCursor Cursor(AuctionEscrowListing l) => new(new(l.State.ExpiresAt.UtcTicks / 10 * 10, TimeSpan.Zero), l.State.ListingId);
    internal static bool After(AuctionEscrowListing l, AuctionCursor? after) => after is null || Cursor(l).ExpiresAt > after.ExpiresAt
        || Cursor(l).ExpiresAt == after.ExpiresAt && l.State.ListingId.CompareTo(after.ListingId) > 0;
    internal static AuctionPage Page(IReadOnlyList<AuctionEscrowListing> found, int limit) => new(found.Take(limit).ToArray(), found.Count > limit ? Cursor(found[limit - 1]) : null);
    internal static (Guid RequestId, TradeParticipant Actor) ReceiptIdentity(AuctionStoredReceipt r) => r.Kind switch
    {
        "escrow" => Escrow(JsonSerializer.Deserialize<AuctionEscrowResult>(r.Json)!),
        "purchase" => Purchase(JsonSerializer.Deserialize<AuctionPurchaseResult>(r.Json)!),
        "claim" => Claim(JsonSerializer.Deserialize<AuctionClaimResult>(r.Json)!),
        _ => throw new InvalidDataException("Invalid auction receipt kind.")
    };
    private static (Guid, TradeParticipant) Escrow(AuctionEscrowResult r) => (r.RequestId, r.Listing.State.Seller);
    private static (Guid, TradeParticipant) Purchase(AuctionPurchaseResult r) => (r.RequestId, r.Listing.Sale!.Buyer);
    private static (Guid, TradeParticipant) Claim(AuctionClaimResult r) => (r.RequestId, r.Claimant);
}
