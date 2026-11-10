namespace ProjectRpg.Backend.Domain.Trading;

public enum AuctionListingStatus { Active, SaleAgreed, Cancelled, Expired }

/// <summary>
/// Immutable proposed listing state. Persist registration WITH item escrow, and a sale WITH payment,
/// delivery, seller payable, fee and receipt in one transaction. SaleAgreed is NOT a completed sale.
/// Authentication, live item/catalog checks, reservations and durable CAS belong to that store.
/// </summary>
public sealed class AuctionListing
{
    public Guid ListingId { get; }
    public TradeParticipant Seller { get; }
    public TradeOfferedItem Item { get; }
    public AuctionTerms Terms { get; }
    public long TotalPrice { get; }
    public DateTimeOffset CreatedAt { get; }
    public DateTimeOffset ExpiresAt { get; }
    public long Revision { get; }
    public AuctionListingStatus Status { get; }
    public TradeParticipant? Buyer { get; }

    private AuctionListing(Guid listingId, TradeParticipant seller, TradeOfferedItem item, AuctionTerms terms,
        long totalPrice, DateTimeOffset createdAt, DateTimeOffset expiresAt, long revision,
        AuctionListingStatus status, TradeParticipant? buyer = null)
    {
        ListingId = listingId; Seller = seller; Item = item; Terms = terms; TotalPrice = totalPrice;
        CreatedAt = createdAt; ExpiresAt = expiresAt; Revision = revision; Status = status; Buyer = buyer;
    }

    /// <summary>Checks the source inventory item. This method does not move or reserve it.</summary>
    public static AuctionListing Prepare(Guid listingId, TradeParticipant seller, ItemRecord sourceItem,
        TradeOfferedItem offered, long totalPrice, DateTimeOffset now, DateTimeOffset expiresAt,
        AuctionTerms terms, TradeItemPolicy policy)
    {
        ArgumentNullException.ThrowIfNull(terms);
        ArgumentNullException.ThrowIfNull(policy);
        if (listingId == Guid.Empty || !AuctionTerms.ValidParticipant(seller))
            throw new TradeRuleException("invalid_auction_listing");
        if (totalPrice < terms.MinimumTotalPrice || totalPrice > terms.MaximumTotalPrice)
            throw new TradeRuleException("invalid_auction_price");
        TimeSpan duration = expiresAt - now;
        if (duration < terms.MinimumDuration || duration > terms.MaximumDuration)
            throw new TradeRuleException("invalid_auction_duration");
        policy.ValidateOfferItem(sourceItem, seller, offered, TradeChannel.Auction, now);
        if (sourceItem.Metadata.ExpiresAtUtc is { } expiry && expiresAt > expiry)
            throw new TradeRuleException("auction_item_expires_before_listing");
        return new(listingId, seller, offered, terms, totalPrice, now.ToUniversalTime(),
            expiresAt.ToUniversalTime(), 1, AuctionListingStatus.Active);
    }

    // Entire listed quantity only; currency and total price must be the ones the buyer reviewed.
    public AuctionSalePlan AgreePurchase(TradeParticipant buyer, long expectedRevision,
        string expectedCurrencyCode, long expectedTotalPrice, DateTimeOffset now)
    {
        if (!AuctionTerms.ValidParticipant(buyer)) throw new TradeRuleException("invalid_auction_buyer");
        if (buyer.AccountId == Seller.AccountId || buyer.CharacterId == Seller.CharacterId)
            throw new TradeRuleException("auction_self_purchase");
        CheckActive(expectedRevision, now);
        if (now >= ExpiresAt) throw new TradeRuleException("auction_expired");
        if (expectedCurrencyCode != Terms.CurrencyCode || expectedTotalPrice != TotalPrice)
            throw new TradeRuleException("auction_price_conflict");
        return new(Next(AuctionListingStatus.SaleAgreed, buyer), AuctionMoney.Sale(TotalPrice, Terms.FeeBasisPoints));
    }

    public AuctionListing Cancel(TradeParticipant actor, long expectedRevision, DateTimeOffset now)
    {
        if (actor != Seller) throw new TradeRuleException("auction_seller_required");
        CheckActive(expectedRevision, now);
        return Next(now >= ExpiresAt ? AuctionListingStatus.Expired : AuctionListingStatus.Cancelled);
    }

    // Trusted server clock/worker only. Persist together with the unsold item's disposition and receipt.
    public AuctionListing Expire(long expectedRevision, DateTimeOffset now)
    {
        CheckActive(expectedRevision, now);
        if (now < ExpiresAt) throw new TradeRuleException("auction_not_expired");
        return Next(AuctionListingStatus.Expired);
    }

    private void CheckActive(long expectedRevision, DateTimeOffset now)
    {
        if (expectedRevision != Revision) throw new TradeRuleException("auction_revision_conflict");
        if (Status != AuctionListingStatus.Active) throw new TradeRuleException("auction_not_active");
        if (now < CreatedAt) throw new TradeRuleException("invalid_auction_time");
    }

    private AuctionListing Next(AuctionListingStatus status, TradeParticipant? buyer = null)
    {
        if (Revision == long.MaxValue) throw new TradeRuleException("auction_revision_exhausted");
        return new(ListingId, Seller, Item, Terms, TotalPrice, CreatedAt, ExpiresAt, Revision + 1, status, buyer);
    }
}

/// <summary>Purchase intent/calculation; neither an escrow proof nor a stored settlement receipt.</summary>
public sealed class AuctionSalePlan
{
    public AuctionListing Listing { get; }
    public AuctionSaleAmounts Amounts { get; }
    internal AuctionSalePlan(AuctionListing listing, AuctionSaleAmounts amounts)
    { Listing = listing; Amounts = amounts; }
}
