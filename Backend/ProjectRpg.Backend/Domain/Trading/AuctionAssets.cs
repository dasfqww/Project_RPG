namespace ProjectRpg.Backend.Domain.Trading;

// Current read snapshot bound to a committed actor request, never the receipt's historical assets.
public sealed record AuctionAssets(Guid RequestId, Guid CharacterId, DateTimeOffset ObservedAt,
    CurrencyWallet Wallet, IReadOnlyList<ItemRecord> Items)
{
    public const int MaximumItems = 500;
    internal static AuctionAssets Create(Guid request, TradeParticipant actor, CurrencyWallet wallet,
        IReadOnlyList<ItemRecord> items, DateTimeOffset now)
    {
        var owner = new ItemOwnerRef(ItemOwnerType.Character, actor.CharacterId.ToString("D"));
        if (request == Guid.Empty || wallet.CharacterId != actor.CharacterId || wallet.AccountId != actor.AccountId
            || items.Count > MaximumItems || items.Any(i => i.Owner != owner || i.LifecycleState != ItemLifecycleState.Active)
            || items.Select(i => i.ItemId).Distinct().Count() != items.Count) throw new TradeRuleException("auction_assets_unavailable");
        return new(request, actor.CharacterId, now, wallet with { Balances = Array.AsReadOnly(wallet.Balances.ToArray()) },
            Array.AsReadOnly(items.Select(i => i with { State = i.State with {
                InstanceTags = Array.AsReadOnly(i.State.InstanceTags.ToArray()), StatValues = Array.AsReadOnly(i.State.StatValues.ToArray()) } }).ToArray()));
    }
}
