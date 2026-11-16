using System.Security.Cryptography;
using System.Text;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Shop;

/// <summary>Pure planning against a transaction-protected snapshot; stores own authorization and atomic persistence.</summary>
public static class ShopPlanner
{
    public static ShopPlan Plan(ShopCommand command, ShopCatalog catalog, IReadOnlyList<ItemRecord> inventory,
        IReadOnlySet<Guid> reservedItemIds, long balance, long availableBalance, long maxBalance, DateTimeOffset now)
    {
        ShopRules.Validate(command);
        ArgumentNullException.ThrowIfNull(catalog); ArgumentNullException.ThrowIfNull(inventory);
        ArgumentNullException.ThrowIfNull(reservedItemIds);
        if (!catalog.Enabled || command.ShopId != catalog.ShopId) throw new ShopRuleException("shop_unavailable");
        if (command.PolicyTag != catalog.PolicyTag) throw new ShopRuleException("shop_policy_changed");
        if (balance < 0 || availableBalance < 0 || availableBalance > balance || maxBalance < balance || now == default)
            throw new ShopRuleException("invalid_shop_wallet");
        var owner = new ItemOwnerRef(ItemOwnerType.Character, command.Actor.CharacterId.ToString("D"));
        if (inventory.Count > InventoryBagLayout.TotalSlots || inventory.Any(i => !ShopRules.ValidRecord(i)
                || i.Owner != owner || i.Revision <= 0 || i.LifecycleState != ItemLifecycleState.Active
                || i.Location.ContainerType != ItemContainerType.Inventory || i.Location.ContainerId != owner.OwnerId
                || !InventoryBagLayout.CanStore(i))
            || inventory.Select(i => i.ItemId).Distinct().Count() != inventory.Count
            || inventory.Select(i => i.Location.SlotIndex).Distinct().Count() != inventory.Count)
            throw new ShopRuleException("invalid_shop_inventory");
        return command.Action == "buy"
            ? Buy(command, catalog, inventory, reservedItemIds, owner, balance, availableBalance)
            : Sell(command, catalog, inventory, reservedItemIds, balance, maxBalance, now);
    }

    private static ShopPlan Buy(ShopCommand c, ShopCatalog catalog, IReadOnlyList<ItemRecord> inventory,
        IReadOnlySet<Guid> reserved, ItemOwnerRef owner, long balance, long available)
    {
        var offer = catalog.Offer(c.OfferId);
        if (c.ExpectedUnitPrice != offer.UnitPrice) throw new ShopRuleException("shop_price_changed");
        if (c.Quantity > offer.MaximumQuantity) throw new ShopRuleException("shop_quantity_limit");
        long total = ShopRules.Total(offer.UnitPrice, c.Quantity);
        if (total > available) throw new ShopRuleException("shop_insufficient_balance");
        Guid itemId = PurchaseItemId(c);
        if (inventory.Any(i => i.ItemId == itemId)) throw new ShopRuleException("shop_item_identity_conflict");
        ItemRecord incoming = new(offer.DefinitionType, offer.DefinitionName, offer.DefinitionVersion, owner,
            new(ItemContainerType.Mail, c.RequestId.ToString("D"), 0),
            new(itemId, 0, c.Quantity, Array.Empty<string>(), Array.Empty<ItemStatValue>()), 0,
            ItemLifecycleState.Active, new(offer.BindState, new(0, 0), null, "ShopPurchase", false));
        var grant = new ItemRepositoryCommitRequest(c.RequestId, "ShopPurchase", ShopRules.Fingerprint(c), owner,
            c.Quantity, [new(0, incoming)]);
        var delivery = DungeonRewardDeliveryPlanner.PlanAuctionReturn(grant, [offer.MaximumStackSize], inventory, reserved);
        if (delivery.Mutations.Any(m => m.NewRecord.Location.ContainerType != ItemContainerType.Inventory))
            throw new ShopRuleException("shop_inventory_full");
        if (delivery.Mutations.Any(m => m.ExpectedRevision == long.MaxValue))
            throw new ShopRuleException("shop_item_revision_exhausted");
        return new(total, balance - total, Array.AsReadOnly(delivery.Mutations.Select(m =>
            new ItemRecordMutation(m.ExpectedRevision, ItemRecordRules.SnapshotWithRevision(m.NewRecord, m.ExpectedRevision))).ToArray()));
    }

    private static ShopPlan Sell(ShopCommand c, ShopCatalog catalog, IReadOnlyList<ItemRecord> inventory,
        IReadOnlySet<Guid> reserved, long balance, long maximum, DateTimeOffset now)
    {
        var rule = catalog.SaleRule(c.OfferId);
        if (c.ExpectedUnitPrice != rule.UnitPrice) throw new ShopRuleException("shop_price_changed");
        if (c.Quantity > rule.MaximumQuantity) throw new ShopRuleException("shop_quantity_limit");
        var item = inventory.SingleOrDefault(i => i.ItemId == c.ItemId) ?? throw new ShopRuleException("shop_item_unavailable");
        ValidateSaleItem(item, rule, c.Quantity, c.ExpectedItemRevision!.Value, reserved.Contains(item.ItemId), now);
        long total = ShopRules.Total(rule.UnitPrice, c.Quantity);
        if (total > maximum - balance) throw new ShopRuleException("shop_wallet_limit");
        int remaining = item.State.Quantity - c.Quantity;
        var after = item with
        {
            State = item.State with { Quantity = remaining },
            Location = remaining == 0 ? new(ItemContainerType.Terminal, "", -1) : item.Location,
            LifecycleState = remaining == 0 ? ItemLifecycleState.Consumed : ItemLifecycleState.Active
        };
        return new(total, balance + total, Array.AsReadOnly(new[]
            { new ItemRecordMutation(item.Revision, ItemRecordRules.SnapshotWithRevision(after, item.Revision)) }));
    }

    public static void ValidateSaleItem(ItemRecord item, ShopSaleRule rule, int quantity, long expectedRevision,
        bool reserved, DateTimeOffset now)
    {
        if (!ShopRules.ValidRecord(item) || rule is null || item.LifecycleState != ItemLifecycleState.Active
            || item.Location.ContainerType != ItemContainerType.Inventory || InventoryBagLayout.Tab(item.DefinitionName) != 2
            || !InventoryBagLayout.CanStore(item) || item.State.EquipmentProgression is not null
            || ShopCatalog.ProtectedName(item.DefinitionName)
            || item.State.InstanceTags.Any(t => t.Split('.').Any(p => p.Equals("Quest", StringComparison.OrdinalIgnoreCase)
                || p.Equals("Story", StringComparison.OrdinalIgnoreCase) || p.Equals("NoSell", StringComparison.OrdinalIgnoreCase)
                || p.Equals("Protected", StringComparison.OrdinalIgnoreCase))))
            throw new ShopRuleException("shop_sale_not_allowed");
        if (expectedRevision <= 0 || expectedRevision == long.MaxValue || item.Revision != expectedRevision)
            throw new ShopRuleException("shop_item_revision_conflict");
        if (item.Metadata.IsLocked || reserved || item.Metadata.ExpiresAtUtc is { } expiry && expiry <= now)
            throw new ShopRuleException("shop_item_unavailable");
        if (item.DefinitionType != rule.DefinitionType || item.DefinitionName != rule.DefinitionName
            || item.DefinitionVersion != rule.DefinitionVersion || !rule.AllowedBindStates.Contains(item.Metadata.BindState))
            throw new ShopRuleException("shop_sale_not_allowed");
        if (quantity <= 0 || quantity > rule.MaximumQuantity || quantity > item.State.Quantity
            || item.State.Quantity > rule.MaximumStackSize) throw new ShopRuleException("shop_quantity_limit");
    }

    public static string? SaleBlockReason(ItemRecord item, ShopSaleRule rule, ShopActor actor,
        bool reserved, DateTimeOffset now)
    {
        if (!ShopRules.ValidActor(actor) || !ShopRules.ValidRecord(item)
            || item.Owner != new ItemOwnerRef(ItemOwnerType.Character, actor.CharacterId.ToString("D"))
            || item.Location.ContainerId != actor.CharacterId.ToString("D")) return "shop_item_unavailable";
        try { ValidateSaleItem(item, rule, 1, item.Revision, reserved, now); return null; }
        catch (ShopRuleException error) { return error.Code; }
    }

    public static Guid PurchaseItemId(ShopCommand command)
    {
        ShopRules.Validate(command);
        return new Guid(SHA256.HashData(Encoding.UTF8.GetBytes("ProjectRpg.Shop.v1:" + ShopRules.Fingerprint(command))).AsSpan(0, 16));
    }
}
