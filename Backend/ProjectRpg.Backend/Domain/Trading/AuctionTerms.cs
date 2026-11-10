namespace ProjectRpg.Backend.Domain.Trading;

/// <summary>Immutable server policy captured by a listing, never a player-supplied fee or currency.</summary>
public sealed class AuctionTerms
{
    public string CurrencyCode { get; }
    public long MinimumTotalPrice { get; }
    public long MaximumTotalPrice { get; }
    public TimeSpan MinimumDuration { get; }
    public TimeSpan MaximumDuration { get; }
    public int FeeBasisPoints { get; }

    public AuctionTerms(string currencyCode, long minimumTotalPrice, long maximumTotalPrice,
        TimeSpan minimumDuration, TimeSpan maximumDuration, int feeBasisPoints)
    {
        if (string.IsNullOrWhiteSpace(currencyCode) || currencyCode.Length > 64
            || !currencyCode.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-')
            || minimumTotalPrice <= 0 || maximumTotalPrice < minimumTotalPrice
            || minimumDuration <= TimeSpan.Zero || maximumDuration < minimumDuration
            || feeBasisPoints is < 0 or > AuctionMoney.BasisPoints)
            throw new TradeRuleException("invalid_auction_terms");
        CurrencyCode = currencyCode;
        MinimumTotalPrice = minimumTotalPrice;
        MaximumTotalPrice = maximumTotalPrice;
        MinimumDuration = minimumDuration;
        MaximumDuration = maximumDuration;
        FeeBasisPoints = feeBasisPoints;
    }

    internal static bool ValidParticipant(TradeParticipant? participant) => participant is not null
        && participant.CharacterId != Guid.Empty && !string.IsNullOrWhiteSpace(participant.AccountId)
        && participant.AccountId.Length <= 128 && !participant.AccountId.Any(char.IsControl);
}
