using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain.Trading;

public sealed record AuctionClaimCommand(Guid RequestId, Guid ListingId, TradeParticipant Claimant,
    long ExpectedRevision, string ExpectedCurrencyCode, long RequestedAmount);
public sealed record AuctionSellerPayable(Guid ListingId, TradeParticipant Seller, CurrencyOwnerRef Wallet,
    string CurrencyCode, long InitialAmount, long RemainingAmount, long Revision);
public sealed record AuctionClaimPayment(CurrencyDefinition Currency, long PreviousBalance, long NewBalance, long PreviousRevision);
public sealed record AuctionClaimResult(Guid RequestId, TradeParticipant Claimant, AuctionSellerPayable Previous,
    AuctionSellerPayable Payable, AuctionClaimPayment Payment, long Paid, DateTimeOffset CommittedAt);

internal static class AuctionClaimRules
{
    internal static void Validate(AuctionClaimCommand c)
    {
        if (c is null || c.RequestId == Guid.Empty || c.ListingId == Guid.Empty || !AuctionTerms.ValidParticipant(c.Claimant)
            || c.ExpectedRevision <= 0 || c.ExpectedRevision == long.MaxValue || c.RequestedAmount <= 0
            || string.IsNullOrWhiteSpace(c.ExpectedCurrencyCode) || c.ExpectedCurrencyCode.Length > 64
            || !c.ExpectedCurrencyCode.All(x => char.IsAsciiLetterOrDigit(x) || x is '.' or '_' or '-'))
            throw new TradeRuleException("invalid_auction_claim");
    }
    internal static string Fingerprint(AuctionClaimCommand c) => Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(c))));
    internal static void Validate(AuctionSellerPayable p)
    {
        if (p is null || p.ListingId == Guid.Empty || !AuctionTerms.ValidParticipant(p.Seller)
            || !AuctionPurchaseRules.ValidOwner(p.Wallet, p.Seller) || string.IsNullOrWhiteSpace(p.CurrencyCode)
            || p.InitialAmount < 0 || p.RemainingAmount < 0 || p.RemainingAmount > p.InitialAmount || p.Revision <= 0)
            throw new InvalidDataException("Invalid stored auction payable.");
    }
    internal static void Validate(AuctionClaimResult r, AuctionClaimCommand c)
    {
        Validate(r.Previous); Validate(r.Payable);
        var before = r.Previous; var after = r.Payable; var payment = r.Payment;
        if (r.RequestId != c.RequestId || r.Claimant != c.Claimant || before.ListingId != c.ListingId
            || before.Seller.AccountId != c.Claimant.AccountId || !AuctionPurchaseRules.ValidOwner(before.Wallet, c.Claimant)
            || before.Revision != c.ExpectedRevision || before.CurrencyCode != c.ExpectedCurrencyCode
            || payment is null || payment.Currency is null || !EconomyRules.TryValidateDefinition(payment.Currency, out _)
            || !payment.Currency.Enabled || payment.Currency.CurrencyCode != before.CurrencyCode || payment.Currency.Scope != before.Wallet.Scope
            || payment.PreviousBalance < 0 || payment.PreviousBalance > payment.Currency.MaxBalance
            || payment.PreviousRevision < 0 || (r.Paid > 0 && payment.PreviousRevision == long.MaxValue))
            throw new InvalidDataException("Invalid auction claim receipt.");
        var amounts = AuctionMoney.Claim(before.RemainingAmount, c.RequestedAmount, payment.PreviousBalance, payment.Currency.MaxBalance);
        if (r.Paid != amounts.Paid || payment.NewBalance != amounts.NewWalletBalance
            || after != before with { RemainingAmount = amounts.RemainingPayable, Revision = before.Revision + (r.Paid > 0 ? 1 : 0) })
            throw new InvalidDataException("Auction claim does not conserve payable and wallet.");
    }
}
