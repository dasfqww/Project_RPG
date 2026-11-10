using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain.Trading;

public enum AuctionEscrowKind { Register, Cancel, Expire }

// Internal server command. Seller, terms and time policy must come from trusted application state.
public sealed record AuctionEscrowCommand(Guid RequestId, Guid ListingId, TradeParticipant Seller, AuctionEscrowKind Kind,
    long ExpectedRevision = 0, TradeOfferedItem? Item = null, long TotalPrice = 0,
    DateTimeOffset ExpiresAt = default, AuctionTerms? Terms = null);
public sealed record AuctionEscrowListing(AuctionListing State, ItemRecord EscrowAtRegistration,
    TradeSplitOrigin? Split, int MaximumStackSize,
    [property: System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    AuctionSaleSettlement? Sale = null);
public sealed record AuctionEscrowResult(Guid RequestId, AuctionEscrowListing Listing,
    IReadOnlyList<ItemRecord> Items, DateTimeOffset CommittedAt);

public sealed class AuctionEscrowLimits
{
    public int MaximumOpenListingsPerAccount { get; }
    public int MaximumOwnedRecords { get; }
    public AuctionEscrowLimits(int maximumOpenListingsPerAccount, int maximumOwnedRecords)
    {
        if (maximumOpenListingsPerAccount is < 1 or > 1000 || maximumOwnedRecords is < 1 or > 500)
            throw new TradeRuleException("invalid_auction_limits");
        MaximumOpenListingsPerAccount = maximumOpenListingsPerAccount;
        MaximumOwnedRecords = maximumOwnedRecords;
    }
}

public static class AuctionEscrowRules
{
    public static ItemOwnerRef Owner(Guid listingId) => new(ItemOwnerType.System, "auction:" + listingId.ToString("D"));
    public static bool IsOwner(ItemOwnerRef owner) => owner is not null && owner.Type == ItemOwnerType.System
        && owner.OwnerId?.StartsWith("auction:", StringComparison.OrdinalIgnoreCase) == true;
    public static Guid SplitItemId(Guid listingId) => new(SHA256.HashData(Encoding.UTF8.GetBytes("auction_split|" + listingId.ToString("D")))[..16]);

    internal static AuctionEscrowCommand Validate(AuctionEscrowCommand c)
    {
        if (c is null || c.RequestId == Guid.Empty || c.ListingId == Guid.Empty || !AuctionTerms.ValidParticipant(c.Seller) || !Enum.IsDefined(c.Kind)
            || (c.Kind == AuctionEscrowKind.Register
                ? c.ExpectedRevision != 0 || c.Item is null || c.Terms is null || c.TotalPrice <= 0
                    || c.Item.ItemId == Guid.Empty || c.Item.ExpectedRevision <= 0 || c.Item.ExpectedRevision == long.MaxValue || c.Item.Quantity <= 0
                : c.ExpectedRevision <= 0 || c.ExpectedRevision == long.MaxValue || c.Item is not null || c.Terms is not null
                    || c.TotalPrice != 0 || c.ExpiresAt != default))
            throw new TradeRuleException("invalid_auction_command");
        return c with { ExpiresAt = c.ExpiresAt.ToUniversalTime() };
    }

    internal static string Fingerprint(AuctionEscrowCommand c) => Convert.ToHexStringLower(
        SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(c))));

    internal static void Validate(AuctionEscrowListing value)
    {
        var s = value.State; var item = value.EscrowAtRegistration;
        if (!ItemRecordRules.TryValidateRecord(item, out _) || item.Owner != Owner(s.ListingId)
            || item.Location != new ItemLocation(ItemContainerType.Auction, s.ListingId.ToString("D"), 0)
            || item.LifecycleState != ItemLifecycleState.Active || item.Metadata.IsLocked
            || item.State.Quantity != s.Item.Quantity || value.MaximumStackSize < item.State.Quantity
            || (value.Split is null
                ? item.ItemId != s.Item.ItemId || item.Revision != s.Item.ExpectedRevision + 1
                : value.Split != new TradeSplitOrigin(s.Item.ItemId, item.ItemId, s.Item.Quantity)
                    || item.ItemId != SplitItemId(s.ListingId) || item.Revision != 1))
            throw new InvalidDataException("Invalid stored auction escrow snapshot.");
        if (s.Status == AuctionListingStatus.SaleAgreed)
        {
            var sale = value.Sale;
            if (sale is null || sale.RequestId == Guid.Empty || sale.Buyer != s.Buyer
                || sale.Currency is null || !EconomyRules.TryValidateDefinition(sale.Currency, out _) || !sale.Currency.Enabled
                || sale.Currency.CurrencyCode != s.Terms.CurrencyCode || !AuctionPurchaseRules.ValidOwner(sale.SellerWallet, s.Seller)
                || sale.SellerWallet.Scope != sale.Currency.Scope || sale.Amounts != AuctionMoney.Sale(s.TotalPrice, s.Terms.FeeBasisPoints)
                || sale.CommittedAt < s.CreatedAt || sale.CommittedAt >= s.ExpiresAt)
                throw new InvalidDataException("Auction sale lacks a valid committed settlement.");
        }
        else if (value.Sale is not null) throw new InvalidDataException("Unsold auction has settlement data.");
    }
}
