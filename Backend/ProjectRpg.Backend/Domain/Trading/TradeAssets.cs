namespace ProjectRpg.Backend.Domain.Trading;

// Current owned assets observed after completion, NOT the immutable exchange/command receipt.
public sealed record TradeAssets(TradeStoredSession Session, Guid CharacterId, DateTimeOffset ObservedAt,
    CurrencyWallet Wallet, IReadOnlyList<ItemRecord> Items)
{
    public const int MaximumItems = 500;

    internal static TradeAssets Create(TradeStoredSession session, TradeParticipant actor, CurrencyWallet wallet,
        IReadOnlyList<ItemRecord> items, DateTimeOffset now)
    {
        if (!session.Completed) throw new TradeRuleException("trade_not_completed");
        var owner = new ItemOwnerRef(ItemOwnerType.Character, actor.CharacterId.ToString("D"));
        if (wallet.CharacterId != actor.CharacterId || wallet.AccountId != actor.AccountId
            || items.Count > MaximumItems || items.Any(i => i.Owner != owner || i.LifecycleState != ItemLifecycleState.Active)
            || items.Select(i => i.ItemId).Distinct().Count() != items.Count)
            throw new TradeRuleException("trade_assets_unavailable");
        // No reference to a Memory repository collection survives the shared read gate.
        var copy = items.Select(i => i with { State = i.State with
        {
            InstanceTags = Array.AsReadOnly(i.State.InstanceTags.ToArray()),
            StatValues = Array.AsReadOnly(i.State.StatValues.ToArray())
        } }).ToArray();
        return new(session, actor.CharacterId, now,
            wallet with { Balances = Array.AsReadOnly(wallet.Balances.ToArray()) }, Array.AsReadOnly(copy));
    }
}
