namespace ProjectRpg.Backend.Domain.Trading;

public sealed record AuctionSaleAmounts(long BuyerPayment, long SellerPayable, long Fee);
public sealed record AuctionClaimAmounts(long Paid, long RemainingPayable, long NewWalletBalance);

/// <summary>Exact integer plans only. No wallet, payable or committed receipt is changed here.</summary>
public static class AuctionMoney
{
    public const int BasisPoints = 10_000;

    // Round the fee down once on the total listing price, including a full int64 price.
    public static AuctionSaleAmounts Sale(long totalPrice, int feeBasisPoints)
    {
        if (totalPrice <= 0 || feeBasisPoints is < 0 or > BasisPoints)
            throw new TradeRuleException("invalid_auction_price");
        long fee = (long)((Int128)totalPrice * feeBasisPoints / BasisPoints);
        return new(totalPrice, totalPrice - fee, fee);
    }

    // The store supplies the locked seller wallet, its definition cap and original payable.
    // A full wallet produces a zero plan; the uncredited remainder is never discarded.
    public static AuctionClaimAmounts Claim(long remainingPayable, long requestedAmount,
        long walletBalance, long walletMaximum)
    {
        if (remainingPayable < 0 || requestedAmount <= 0 || walletBalance < 0 || walletMaximum < walletBalance)
            throw new TradeRuleException("invalid_auction_claim");
        long paid = Math.Min(Math.Min(remainingPayable, requestedAmount), walletMaximum - walletBalance);
        return new(paid, remainingPayable - paid, walletBalance + paid);
    }
}
