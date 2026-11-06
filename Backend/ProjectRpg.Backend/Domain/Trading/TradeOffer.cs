using System.Collections.ObjectModel;

namespace ProjectRpg.Backend.Domain.Trading;

public sealed class TradeRuleException(string code) : Exception(code)
{
    public string Code { get; } = code;
}

public sealed record TradeParticipant(string AccountId, Guid CharacterId);
public sealed record TradeOfferedItem(Guid ItemId, long ExpectedRevision, int Quantity);

/// <summary>A snapshot of an offer, not evidence of ownership or an asset reservation.</summary>
public sealed class TradeOffer
{
    public const int MaximumItemCount = 8;
    public static TradeOffer Empty { get; } = new([], 0);
    public IReadOnlyList<TradeOfferedItem> Items { get; }
    public long CurrencyAmount { get; }
    public bool IsEmpty => Items.Count == 0 && CurrencyAmount == 0;

    [System.Text.Json.Serialization.JsonConstructor]
    private TradeOffer(IReadOnlyList<TradeOfferedItem> items, long currencyAmount)
        : this((IEnumerable<TradeOfferedItem>)items, currencyAmount) { }

    public TradeOffer(IEnumerable<TradeOfferedItem> items, long currencyAmount)
    {
        ArgumentNullException.ThrowIfNull(items);
        // Bound enumeration as well as the accepted collection.
        TradeOfferedItem[] snapshot = items.Take(MaximumItemCount + 1).ToArray();
        if (currencyAmount < 0 || snapshot.Length > MaximumItemCount
            || snapshot.Any(item => item is null || item.ItemId == Guid.Empty
                || item.ExpectedRevision <= 0 || item.ExpectedRevision == long.MaxValue || item.Quantity <= 0)
            || snapshot.Select(item => item.ItemId).Distinct().Count() != snapshot.Length)
            throw new TradeRuleException("invalid_offer");
        Items = Array.AsReadOnly(snapshot.OrderBy(item => item.ItemId).ToArray());
        CurrencyAmount = currencyAmount;
    }
}
