using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain.Trading;

// Internal intent: authenticate Buyer in the application; no wallet owner/delta supplied by a player.
public sealed record AuctionPurchaseCommand(Guid RequestId, Guid ListingId, TradeParticipant Buyer,
    long ExpectedRevision, string ExpectedCurrencyCode, long ExpectedTotalPrice);
public sealed record AuctionSaleSettlement(Guid RequestId, TradeParticipant Buyer, CurrencyDefinition Currency,
    CurrencyOwnerRef SellerWallet, AuctionSaleAmounts Amounts, DateTimeOffset CommittedAt);
public sealed record AuctionBuyerPayment(CurrencyOwnerRef Wallet, long PreviousBalance, long NewBalance, long PreviousRevision);
public sealed record AuctionPurchaseResult(Guid RequestId, AuctionEscrowListing Listing,
    AuctionBuyerPayment Payment, IReadOnlyList<ItemRecord> Items, DateTimeOffset CommittedAt);

internal static class AuctionPurchaseRules
{
    internal static void Validate(AuctionPurchaseCommand c)
    {
        if (c is null || c.RequestId == Guid.Empty || c.ListingId == Guid.Empty || !AuctionTerms.ValidParticipant(c.Buyer)
            || c.ExpectedRevision <= 0 || c.ExpectedRevision == long.MaxValue || c.ExpectedTotalPrice <= 0
            || string.IsNullOrWhiteSpace(c.ExpectedCurrencyCode) || c.ExpectedCurrencyCode.Length > 64
            || !c.ExpectedCurrencyCode.All(x => char.IsAsciiLetterOrDigit(x) || x is '.' or '_' or '-'))
            throw new TradeRuleException("invalid_auction_purchase");
    }

    internal static string Fingerprint(AuctionPurchaseCommand c) => Convert.ToHexStringLower(
        SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(c))));

    internal static bool ValidOwner(CurrencyOwnerRef owner, TradeParticipant person) => owner is not null
        && Enum.IsDefined(owner.Scope) && (owner.Scope == CurrencyScope.Account ? owner.OwnerId == person.AccountId
            : owner.Scope == CurrencyScope.Character ? owner.OwnerId == person.CharacterId.ToString("D")
            : Guid.TryParseExact(owner.OwnerId, "D", out var id) && id != Guid.Empty);

    internal static void Validate(AuctionPurchaseResult result, AuctionPurchaseCommand c)
    {
        AuctionEscrowRules.Validate(result.Listing);
        var sale = result.Listing.Sale; var payment = result.Payment;
        if (sale is null || sale.Buyer != c.Buyer || result.RequestId != c.RequestId || sale.RequestId != c.RequestId
            || result.Listing.State.ListingId != c.ListingId || result.Listing.State.Terms.CurrencyCode != c.ExpectedCurrencyCode
            || result.Listing.State.TotalPrice != c.ExpectedTotalPrice || result.CommittedAt != sale.CommittedAt
            || result.Listing.State.Revision != c.ExpectedRevision + 1
            || result.Items is null || result.Items.Count == 0 || result.Items.Count > DungeonRewardDeliveryPlanner.InventorySlotCapacity + 1
            || result.Items.Select(i => i.ItemId).Distinct().Count() != result.Items.Count
            || result.Items.Any(i => !ItemRecordRules.TryValidateRecord(i, out _) || i.Revision <= 0)
            || payment is null || !ValidOwner(payment.Wallet, c.Buyer) || payment.Wallet.Scope != sale.Currency.Scope
            || payment.Wallet == sale.SellerWallet || payment.PreviousBalance < sale.Amounts.BuyerPayment
            || payment.NewBalance != payment.PreviousBalance - sale.Amounts.BuyerPayment
            || payment.NewBalance < 0 || payment.NewBalance > sale.Currency.MaxBalance
            || payment.PreviousRevision < 0 || payment.PreviousRevision == long.MaxValue)
            throw new InvalidDataException("Invalid auction purchase receipt.");
    }
}
