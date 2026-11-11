using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Configuration;

// Disabled by default. These inactive policy defaults do not authorize any production item.
public sealed class AuctionOptions
{
    public bool Enabled { get; set; }
    public string CurrencyCode { get; set; } = "Gold";
    public long MinimumPrice { get; set; } = 1;
    public long MaximumPrice { get; set; } = long.MaxValue;
    public int MinimumDurationSeconds { get; set; } = 60;
    public int MaximumDurationSeconds { get; set; } = 7 * 24 * 60 * 60;
    public int FeeBasisPoints { get; set; } = 500;
    public int MaximumOpenListingsPerAccount { get; set; } = 10;
    public int MaximumOwnedRecords { get; set; } = 500;
    public AuctionTerms Terms() => new(CurrencyCode, MinimumPrice, MaximumPrice, TimeSpan.FromSeconds(MinimumDurationSeconds), TimeSpan.FromSeconds(MaximumDurationSeconds), FeeBasisPoints);
    public void Validate()
    {
        if (MinimumDurationSeconds <= 0 || MaximumDurationSeconds > 30 * 24 * 60 * 60) throw new InvalidOperationException("Invalid Auctions duration policy.");
        _ = Terms(); _ = new AuctionEscrowLimits(MaximumOpenListingsPerAccount, MaximumOwnedRecords);
    }
}
