namespace ProjectRpg.Backend.Domain.Trading;

public enum TradeChannel { Direct, Auction }

public sealed record TradeDefinitionPolicy(
    string DefinitionType, string DefinitionName, int DefinitionVersion,
    bool AllowDirect, bool AllowAuction, int MaximumStackSize);

/// <summary>Server-owned allowlist. Never populated from the player's command or item tags.</summary>
public sealed class TradeItemPolicy
{
    private readonly Dictionary<(string Type, string Name, int Version), TradeDefinitionPolicy> definitions = [];

    public TradeItemPolicy(IEnumerable<TradeDefinitionPolicy> definitions)
    {
        ArgumentNullException.ThrowIfNull(definitions);
        foreach (TradeDefinitionPolicy definition in definitions)
        {
            if (definition is null || !ValidText(definition.DefinitionType) || !ValidText(definition.DefinitionName)
                || definition.DefinitionVersion <= 0 || definition.MaximumStackSize <= 0
                || !this.definitions.TryAdd((definition.DefinitionType, definition.DefinitionName,
                    definition.DefinitionVersion), definition))
                throw new TradeRuleException("invalid_trade_catalog");
        }
    }

    /// <summary>Checks a stored inventory record; reservations, account access and capacity belong to the store.</summary>
    public void ValidateOfferItem(ItemRecord item, TradeParticipant seller, TradeOfferedItem offered,
        TradeChannel channel, DateTimeOffset now)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(seller);
        ArgumentNullException.ThrowIfNull(offered);
        if (!Enum.IsDefined(channel) || seller.CharacterId == Guid.Empty
            || !ValidText(seller.AccountId) || !ItemRecordRules.TryValidateRecord(item, out _))
            throw new TradeRuleException("invalid_trade_item");
        if (item.ItemId != offered.ItemId || offered.ExpectedRevision <= 0
            || offered.ExpectedRevision == long.MaxValue || item.Revision != offered.ExpectedRevision)
            throw new TradeRuleException("item_revision_conflict");
        string characterId = seller.CharacterId.ToString("D");
        if (item.Owner.Type != ItemOwnerType.Character || item.Owner.OwnerId != characterId
            || item.Location.ContainerType != ItemContainerType.Inventory || item.Location.ContainerId != characterId)
            throw new TradeRuleException("item_not_in_owned_inventory");
        ValidateAvailability(item, offered.Quantity, channel, now);
    }

    // The store has locked and matched the real vault record with this listing's registration snapshot.
    internal void ValidateAuctionEscrow(ItemRecord item, AuctionEscrowListing listing, DateTimeOffset now)
    {
        if (!ItemRecordRules.TryValidateRecord(item, out _) || item.Owner != AuctionEscrowRules.Owner(listing.State.ListingId)
            || item.Location != new ItemLocation(ItemContainerType.Auction, listing.State.ListingId.ToString("D"), 0)
            || item.ItemId != listing.EscrowAtRegistration.ItemId || item.Revision != listing.EscrowAtRegistration.Revision
            || item.State.Quantity != listing.State.Item.Quantity)
            throw new TradeRuleException("invalid_trade_item");
        ValidateAvailability(item, listing.State.Item.Quantity, TradeChannel.Auction, now);
    }

    private void ValidateAvailability(ItemRecord item, int quantity, TradeChannel channel, DateTimeOffset now)
    {
        if (item.LifecycleState != ItemLifecycleState.Active || item.Metadata.IsLocked
            || item.Metadata.ExpiresAtUtc is { } expiry && expiry <= now)
            throw new TradeRuleException("item_unavailable");
        if (item.Metadata.BindState is not (ItemBindState.Unbound or ItemBindState.BindOnEquip))
            throw new TradeRuleException("item_bound");
        if (!definitions.TryGetValue((item.DefinitionType, item.DefinitionName, item.DefinitionVersion), out var policy)
            || !(channel == TradeChannel.Direct ? policy.AllowDirect : policy.AllowAuction))
            throw new TradeRuleException("item_trade_not_allowed");
        if (quantity <= 0 || quantity > item.State.Quantity || item.State.Quantity > policy.MaximumStackSize)
            throw new TradeRuleException("invalid_trade_quantity");
    }

    internal int MaximumStackSize(ItemRecord item, TradeChannel channel = TradeChannel.Direct) =>
        definitions.TryGetValue((item.DefinitionType, item.DefinitionName, item.DefinitionVersion), out var definition)
            && (channel == TradeChannel.Direct ? definition.AllowDirect : definition.AllowAuction) ? definition.MaximumStackSize
            : throw new TradeRuleException("item_trade_not_allowed");

    private static bool ValidText(string value) => !string.IsNullOrWhiteSpace(value)
        && value.Length <= 128 && !value.Any(char.IsControl);
}
