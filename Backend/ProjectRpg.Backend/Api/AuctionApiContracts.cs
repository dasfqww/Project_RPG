using System.Globalization;
using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Api;

public sealed record AuctionCommandRequest(Guid RequestId, Guid? ItemId = null, string? ExpectedItemRevision = null, int? Quantity = null,
    string? TotalPrice = null, DateTimeOffset? ExpiresAt = null, string? ExpectedRevision = null, string? ExpectedCurrencyCode = null, string? RequestedAmount = null,
    string? ExpectedPolicyTag = null);

public static class AuctionApiContracts
{
    public static object RegistrationPolicy(AuctionRegistrationPolicy p) => new { p.PolicyTag, p.Terms.CurrencyCode,
        minimumTotalPrice = Text(p.Terms.MinimumTotalPrice), maximumTotalPrice = Text(p.Terms.MaximumTotalPrice),
        minimumDurationSeconds = p.Terms.MinimumDuration.Ticks / TimeSpan.TicksPerSecond,
        maximumDurationSeconds = p.Terms.MaximumDuration.Ticks / TimeSpan.TicksPerSecond, p.Terms.FeeBasisPoints, p.ObservedAt };
    public static object Assets(AuctionAssets r) => new { r.RequestId, r.CharacterId, r.ObservedAt, isComplete = true,
        wallet = EconomyApiContractMapper.ToContract(r.Wallet), items = r.Items.Select(ItemApiContractMapper.ToContract).ToArray() };
    internal static TradeRuleException Invalid() => new("invalid_auction_command");
    internal static long Integer(string? s, bool revision = false)
    {
        if (s is null || s.Length > 19 || !long.TryParse(s, NumberStyles.None, CultureInfo.InvariantCulture, out long n)
            || n <= 0 || revision && n == long.MaxValue || s != Text(n)) throw Invalid();
        return n;
    }
    internal static void Shape(AuctionCommandRequest r, string action)
    {
        if (r.RequestId == Guid.Empty || r.ExpectedPolicyTag is not null && (action != "register" || !AuctionRegistrationPolicy.ValidTag(r.ExpectedPolicyTag))
            || action != "register" && (r.ItemId is not null || r.ExpectedItemRevision is not null || r.Quantity is not null || r.ExpiresAt is not null)
            || action == "register" && (r.ExpectedRevision is not null || r.ExpectedCurrencyCode is not null || r.RequestedAmount is not null)
            || action == "cancel" && (r.TotalPrice is not null || r.ExpectedCurrencyCode is not null || r.RequestedAmount is not null)
            || action == "purchase" && r.RequestedAmount is not null || action == "claim" && r.TotalPrice is not null) throw Invalid();
    }
    internal static AuctionSearch Query(IQueryCollection values)
    {
        string[] allowed = ["limit", "definitionName", "currencyCode", "minimumPrice", "maximumPrice", "cursor"];
        if (values.Any(p => !allowed.Contains(p.Key, StringComparer.Ordinal) || p.Value.Count != 1)) throw new TradeRuleException("invalid_auction_query");
        string? Get(string key) => values.TryGetValue(key, out var v) ? v[0] : null;
        try
        {
            int limit = Get("limit") is { } l ? checked((int)Integer(l)) : 20;
            AuctionCursor? cursor = null;
            if (Get("cursor") is { } encoded)
            {
                if (encoded.Length is 0 or > 512) throw new FormatException();
                var bytes = Convert.FromBase64String(encoded.Replace('-', '+').Replace('_', '/').PadRight((encoded.Length + 3) / 4 * 4, '='));
                cursor = JsonSerializer.Deserialize<AuctionCursor>(bytes) ?? throw new FormatException();
            }
            var q = new AuctionSearch(limit, Get("definitionName"), Get("currencyCode"), Get("minimumPrice") is { } min ? Integer(min) : null,
                Get("maximumPrice") is { } max ? Integer(max) : null, cursor); AuctionQueryRules.Validate(q); return q;
        }
        catch (Exception e) when (e is FormatException or OverflowException or JsonException or TradeRuleException) { throw new TradeRuleException("invalid_auction_query"); }
    }
    public static object Page(AuctionPage page, bool own) => new { listings = page.Listings.Select(l => Listing(l, own)).ToArray(),
        nextCursor = page.Next is null ? null : Convert.ToBase64String(JsonSerializer.SerializeToUtf8Bytes(page.Next)).TrimEnd('=').Replace('+', '-').Replace('/', '_') };
    public static object Listing(AuctionEscrowListing l, bool own)
    {
        var s = l.State; var i = l.EscrowAtRegistration;
        return new { listingId = s.ListingId, revision = Text(s.Revision), status = s.Status == AuctionListingStatus.SaleAgreed ? "Sold" : s.Status.ToString(),
            currencyCode = s.Terms.CurrencyCode, totalPrice = Text(s.TotalPrice), s.ExpiresAt,
            sellerCharacterId = own ? (Guid?)s.Seller.CharacterId : null,
            sellerProceeds = own && l.Sale is not null ? Text(l.Sale.Amounts.SellerPayable) : null,
            item = new { i.DefinitionType, i.DefinitionName, i.DefinitionVersion, quantity = s.Item.Quantity, i.State.InstanceTags, i.State.StatValues,
                i.State.EquipmentProgression, bindState = i.Metadata.BindState.ToString(), i.Metadata.Durability, i.Metadata.ExpiresAtUtc } };
    }
    public static object Result(AuctionEscrowResult r) => new { kind = "escrow", r.RequestId, listing = Listing(r.Listing, true),
        items = r.Items.Where(i => i.Owner == new ItemOwnerRef(ItemOwnerType.Character, r.Listing.State.Seller.CharacterId.ToString("D"))).Select(ItemApiContractMapper.ToContract).ToArray(), r.CommittedAt };
    public static object Result(AuctionPurchaseResult r) => new { kind = "purchase", r.RequestId, listing = Listing(r.Listing, false),
        balance = Text(r.Payment.NewBalance),
        items = r.Items.Where(i => i.Owner == new ItemOwnerRef(ItemOwnerType.Character, r.Listing.Sale!.Buyer.CharacterId.ToString("D"))).Select(ItemApiContractMapper.ToContract).ToArray(), r.CommittedAt };
    public static object Result(AuctionClaimResult r) => new { kind = "claim", r.RequestId, listingId = r.Payable.ListingId,
        revision = Text(r.Payable.Revision), r.Payable.CurrencyCode, paid = Text(r.Paid), remaining = Text(r.Payable.RemainingAmount), balance = Text(r.Payment.NewBalance), r.CommittedAt };
    public static object Payable(AuctionSellerPayable p) => new { listingId = p.ListingId, revision = Text(p.Revision), p.CurrencyCode,
        initialAmount = Text(p.InitialAmount), remainingAmount = Text(p.RemainingAmount) };
    public static object Receipt(AuctionStoredReceipt r) => r.Kind switch
    {
        "escrow" => Result(JsonSerializer.Deserialize<AuctionEscrowResult>(r.Json)!),
        "purchase" => Result(JsonSerializer.Deserialize<AuctionPurchaseResult>(r.Json)!),
        "claim" => Result(JsonSerializer.Deserialize<AuctionClaimResult>(r.Json)!),
        _ => throw new InvalidDataException("Invalid auction receipt kind.")
    };
    private static string Text(long n) => n.ToString(CultureInfo.InvariantCulture);
}
