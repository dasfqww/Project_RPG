using Microsoft.Extensions.Configuration;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Shop;

namespace ProjectRpg.Backend.ShopTests;

public static class DomainCases
{
    private static readonly ShopActor Actor = new("76561198000000001", Guid.Parse("9c31aeb4-4198-4d9d-8837-fdfbcb9c878f"));
    private static readonly DateTimeOffset Now = new(2026, 11, 17, 1, 0, 0, TimeSpan.Zero);
    private static ShopOffer Potion => new("potion", "회복 물약", "GameItem", "GameItem.Consume.Potion.Red.Large", 1, 10, 100, 100, ItemBindState.Unbound);
    private static ShopSaleRule Junk => new("scrap", "판매용 파편", "GameItem", "GameItem.Junk.Scrap", 1, 2, 100, 100, [ItemBindState.Unbound]);
    private static ShopCatalog Catalog(ShopOffer? buy = null, ShopSaleRule? sale = null) =>
        new(true, "general", "보급 상인", "RosterGold", [buy ?? Potion], [sale ?? Junk]);
    private static ShopCommand Buy(ShopCatalog catalog, int quantity = 2) =>
        new(Guid.Parse("13c66159-3c3f-48d7-99a9-6d880b63137d"), Actor, catalog.ShopId, catalog.PolicyTag, "buy", "potion", quantity, 10);
    private static ShopCommand Sell(ShopCatalog catalog, ItemRecord item, int quantity = 2) => Buy(catalog, quantity) with
        { Action = "sell", OfferId = "scrap", ExpectedUnitPrice = 2, ItemId = item.ItemId, ExpectedItemRevision = item.Revision };
    private static ItemRecord Item(string name, int slot, int quantity = 4, long revision = 2) => new("GameItem", name, 1,
        new(ItemOwnerType.Character, Actor.CharacterId.ToString("D")), new(ItemContainerType.Inventory, Actor.CharacterId.ToString("D"), slot),
        new(Guid.NewGuid(), 0, quantity, [], []), revision, ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, "WorldPickup", false));
    private static ShopPlan Plan(ShopCommand command, ShopCatalog catalog, IReadOnlyList<ItemRecord>? inventory = null,
        IReadOnlySet<Guid>? reserved = null, long balance = 100, long available = 100, long maximum = 1000) =>
        ShopPlanner.Plan(command, catalog, inventory ?? [], reserved ?? new HashSet<Guid>(), balance, available, maximum, Now);

    public static void Run(Action<bool, string> assert)
    {
        void Reject(string expected, Action action)
        {
            try { action(); assert(false, "expected " + expected); }
            catch (ShopRuleException e) { assert(e.Code == expected, "rejected " + expected + ": " + e.Code); }
        }
        void InvalidConfig(IConfiguration configuration)
        {
            try { ShopCatalogConfiguration.Load(configuration); assert(false, "bad configuration rejected"); }
            catch (InvalidOperationException) { assert(true, "bad configuration rejected"); }
        }
        var catalog = Catalog(); var buy = Buy(catalog);
        assert(ShopRules.ValidPolicyTag(catalog.PolicyTag), "catalog policy uses bounded lowercase sha256");
        assert(Catalog().PolicyTag == catalog.PolicyTag, "stable catalog identity");
        assert(Catalog(Potion with { UnitPrice = 11 }).PolicyTag != catalog.PolicyTag, "price changes consent identity");
        var second = Potion with { OfferId = "small", DefinitionName = "GameItem.Consume.Potion.Red.Small" };
        var orderA = new ShopCatalog(true, "general", "보급 상인", "RosterGold", [Potion, second], [Junk]);
        var orderB = new ShopCatalog(true, "general", "보급 상인", "RosterGold", [second, Potion], [Junk]);
        assert(orderA.PolicyTag == orderB.PolicyTag, "catalog entry order does not affect policy");
        var sourceBindings = new List<ItemBindState> { ItemBindState.Unbound };
        var sourceOffers = new[] { Potion };
        var frozen = new ShopCatalog(true, "general", "보급 상인", "RosterGold", sourceOffers, [Junk with { AllowedBindStates = sourceBindings }]);
        sourceBindings.Add(ItemBindState.CharacterBound); sourceOffers[0] = Potion with { UnitPrice = 999 };
        assert(frozen.PolicyTag == catalog.PolicyTag && frozen.Offers[0].UnitPrice == 10
            && frozen.SaleRules[0].AllowedBindStates.Count == 1, "catalog owns immutable copies");
        Reject("invalid_shop_catalog", () => Catalog(Potion with { DefinitionName = "GameItem.Equipment.Sword" }));
        Reject("invalid_shop_catalog", () => Catalog(sale: Junk with { DefinitionName = "GameItem.Material.Progression.Ore" }));
        Reject("invalid_shop_catalog", () => Catalog(sale: Junk with { DefinitionName = "GameItem.Quest.Token" }));
        Reject("invalid_shop_catalog", () => Catalog(Potion with { UnitPrice = long.MaxValue }));
        Reject("invalid_shop_catalog", () => Catalog(Potion with { MaximumQuantity = 101 }));
        Reject("invalid_shop_catalog", () => new ShopCatalog(true, "general", "상점", "Gold", [Potion, Potion], []));
        Reject("invalid_shop_catalog", () => Catalog(sale: Junk with { AllowedBindStates = [ItemBindState.Unbound, ItemBindState.Unbound] }));

        var initial = Plan(buy, catalog);
        assert(initial.TotalPrice == 20 && initial.BalanceAfter == 80, "buy debits exact integer total");
        assert(initial.Mutations.Count == 1 && initial.Mutations[0].ExpectedRevision == 0
            && initial.Mutations[0].NewRecord.State.Quantity == 2 && initial.Mutations[0].NewRecord.Location.SlotIndex == 80,
            "purchase creates item in consumption tab");
        assert(initial.Mutations[0].NewRecord.ItemId == Plan(buy, catalog).Mutations[0].NewRecord.ItemId,
            "same request deterministically plans same new item");
        assert(ShopPlanner.PurchaseItemId(buy) != ShopPlanner.PurchaseItemId(buy with { RequestId = Guid.NewGuid() }),
            "different request has different item identity");
        var stack = Item(Potion.DefinitionName, 80, 99);
        var split = Plan(buy, catalog, [stack]);
        assert(split.Mutations.Count == 2 && split.Mutations.Single(m => m.NewRecord.ItemId == stack.ItemId).NewRecord.State.Quantity == 100
            && split.Mutations.Single(m => m.ExpectedRevision == 0).NewRecord.State.Quantity == 1, "buy merges then creates remainder");
        assert(stack.State.Quantity == 99 && stack.Revision == 2, "planning does not mutate input");
        var held = Plan(buy, catalog, [stack], new HashSet<Guid> { stack.ItemId });
        assert(held.Mutations.Count == 1 && held.Mutations[0].NewRecord.Location.SlotIndex == 81, "reserved stack is not changed");
        var full = Enumerable.Range(80, 80).Select(slot => Item(Potion.DefinitionName, slot, 100)).ToArray();
        Reject("shop_inventory_full", () => Plan(buy, catalog, full));
        assert(full.All(i => i.State.Quantity == 100), "capacity failure preserves inventory");
        full[0] = full[0] with { State = full[0].State with { Quantity = 98 } };
        assert(Plan(buy, catalog, full).Mutations.Count == 1, "full tab can use available compatible stack");
        Reject("shop_inventory_full", () => Plan(buy, catalog, full, new HashSet<Guid> { full[0].ItemId }));
        Reject("shop_insufficient_balance", () => Plan(buy, catalog, available: 19));
        Reject("invalid_shop_wallet", () => Plan(buy, catalog, available: 101));
        Reject("shop_price_changed", () => Plan(buy with { ExpectedUnitPrice = 11 }, catalog));
        Reject("shop_policy_changed", () => Plan(buy with { PolicyTag = new string('0', 64) }, catalog));
        Reject("shop_unavailable", () => Plan(buy, new(false, "general", "보급 상인", "RosterGold", [Potion], [Junk])));
        Reject("shop_quantity_limit", () => Plan(buy with { Quantity = 101 }, catalog));
        Reject("shop_item_revision_exhausted", () => Plan(buy, catalog, [stack with { Revision = long.MaxValue }]));
        Reject("invalid_shop_inventory", () => Plan(buy, catalog, [stack, stack]));
        Reject("invalid_shop_inventory", () => Plan(buy, catalog, [stack with { State = null! }]));
        Reject("invalid_shop_inventory", () => Plan(buy, catalog, [stack with { Owner = new(ItemOwnerType.Character, Guid.NewGuid().ToString("D")) }]));
        Reject("invalid_shop_inventory", () => Plan(buy, catalog, [stack with { Location = stack.Location with { SlotIndex = 0 } }]));
        Reject("invalid_shop_command", () => ShopRules.Validate(buy with { ItemId = Guid.NewGuid() }));
        Reject("invalid_shop_command", () => ShopRules.Validate(buy with { Actor = null! }));
        Reject("invalid_shop_command", () => ShopRules.Validate(buy with { Quantity = 0 }));
        Reject("shop_price_overflow", () => ShopRules.Validate(buy with { ExpectedUnitPrice = long.MaxValue }));

        var junk = Item(Junk.DefinitionName, 160); var sell = Sell(catalog, junk);
        var partial = Plan(sell, catalog, [junk]);
        assert(partial.TotalPrice == 4 && partial.BalanceAfter == 104 && partial.Mutations.Single().NewRecord.State.Quantity == 2,
            "partial sale consumes exact amount and credits exact price");
        assert(partial.Mutations.Single().NewRecord.ItemId == junk.ItemId && junk.State.Quantity == 4, "partial sale preserves identity and input");
        var terminal = Plan(sell with { Quantity = 4 }, catalog, [junk]).Mutations.Single().NewRecord;
        assert(terminal.State.Quantity == 0 && terminal.LifecycleState == ItemLifecycleState.Consumed
            && terminal.Location == new ItemLocation(ItemContainerType.Terminal, "", -1), "whole sale retains terminal tombstone");
        Reject("shop_wallet_limit", () => Plan(sell, catalog, [junk], maximum: 103));
        Reject("shop_item_revision_conflict", () => Plan(sell with { ExpectedItemRevision = 1 }, catalog, [junk]));
        Reject("shop_quantity_limit", () => Plan(sell with { Quantity = 5 }, catalog, [junk]));
        Reject("shop_item_unavailable", () => Plan(sell, catalog, [junk], new HashSet<Guid> { junk.ItemId }));
        Reject("shop_item_unavailable", () => Plan(sell, catalog, [junk with { Metadata = junk.Metadata with { IsLocked = true } }]));
        Reject("shop_item_unavailable", () => Plan(sell, catalog, [junk with { Metadata = junk.Metadata with { ExpiresAtUtc = Now } }]));
        Reject("shop_sale_not_allowed", () => Plan(sell, catalog, [junk with { State = junk.State with { InstanceTags = ["Item.Quest.Required"] } }]));
        Reject("shop_sale_not_allowed", () => Plan(sell, catalog, [junk with { State = junk.State with { InstanceTags = ["Item.Protected"] } }]));
        Reject("shop_sale_not_allowed", () => Plan(sell, catalog, [junk with { Metadata = junk.Metadata with { BindState = ItemBindState.CharacterBound } }]));
        var boundCatalog = Catalog(sale: Junk with { AllowedBindStates = [ItemBindState.CharacterBound] });
        var bound = junk with { Metadata = junk.Metadata with { BindState = ItemBindState.CharacterBound } };
        assert(Plan(Sell(boundCatalog, bound), boundCatalog, [bound]).BalanceAfter == 104, "binding permission is an explicit independent sale policy");
        assert(ShopPlanner.SaleBlockReason(junk, Junk, Actor, false, Now) is null, "eligible sale hint");
        assert(ShopPlanner.SaleBlockReason(junk, Junk, Actor with { CharacterId = Guid.NewGuid() }, false, Now) == "shop_item_unavailable", "foreign owner sale hint denied");

        var receipt = new ShopReceipt(buy.RequestId, buy.Actor, buy.ShopId, buy.PolicyTag, buy.Action, buy.OfferId, null,
            buy.Quantity, buy.ExpectedUnitPrice, initial.TotalPrice, catalog.CurrencyCode, 100, initial.BalanceAfter,
            initial.Mutations.Select(m => m.NewRecord with { Revision = m.ExpectedRevision + 1 }).ToArray(), Now);
        ShopRules.ValidateReceipt(receipt, buy); assert(true, "valid purchase receipt");
        try { ShopRules.ValidateReceipt(receipt with { BalanceAfter = 99 }, buy); assert(false, "tampered receipt rejected"); }
        catch (InvalidDataException) { assert(true, "tampered receipt rejected"); }
        var saleReceipt = new ShopReceipt(sell.RequestId, sell.Actor, sell.ShopId, sell.PolicyTag, sell.Action, sell.OfferId,
            sell.ItemId, sell.Quantity, sell.ExpectedUnitPrice, partial.TotalPrice, catalog.CurrencyCode, 100, partial.BalanceAfter,
            partial.Mutations.Select(m => m.NewRecord with { Revision = m.ExpectedRevision + 1 }).ToArray(), Now);
        ShopRules.ValidateReceipt(saleReceipt, sell); assert(true, "valid sale receipt");
        ShopRejectionRules.Validate(new("shop_policy_changed", Now));
        assert(ShopRejectionRules.IsDurable("shop_requires_lobby")
            && !ShopRejectionRules.IsDurable("shop_lease_busy")
            && !ShopRejectionRules.IsDurable("invalid_shop_command")
            && !ShopRejectionRules.IsDurable("shop_revision_conflict")
            && !ShopRejectionRules.IsDurable("service_draining"),
            "only explicit pre-mutation business decisions are terminal rejections");
        try { ShopRejectionRules.Validate(new("shop_storage_unavailable", Now)); assert(false, "transient rejection excluded"); }
        catch (InvalidDataException) { assert(true, "transient rejection excluded"); }
        try { ShopRejectionRules.Validate(new("shop_disabled", default)); assert(false, "missing rejection timestamp excluded"); }
        catch (InvalidDataException) { assert(true, "missing rejection timestamp excluded"); }
        assert(ShopRules.Fingerprint(buy) != ShopRules.Fingerprint(buy with { Quantity = 1 })
            && ShopRules.Fingerprint(buy) != ShopRules.Fingerprint(buy with { Actor = Actor with { CharacterId = Guid.NewGuid() } }),
            "request fingerprint binds quantity and actor");

        IConfiguration Config(Dictionary<string, string?> data) => new ConfigurationBuilder().AddInMemoryCollection(data).Build();
        assert(!ShopCatalogConfiguration.Load(Config([])).Enabled, "omitted shop is disabled");
        assert(!ShopCatalogConfiguration.Load(Config(new() { ["Shop:Enabled"] = "false" })).Enabled, "explicit disabled shop without products");
        var settings = new Dictionary<string, string?>
        {
            ["Shop:Enabled"] = "true", ["Shop:ShopId"] = "general", ["Shop:DisplayName"] = "보급 상인", ["Shop:CurrencyCode"] = "RosterGold",
            ["Shop:Offers:0:OfferId"] = "potion", ["Shop:Offers:0:DisplayName"] = "회복 물약", ["Shop:Offers:0:DefinitionType"] = "GameItem",
            ["Shop:Offers:0:DefinitionName"] = Potion.DefinitionName, ["Shop:Offers:0:DefinitionVersion"] = "1",
            ["Shop:Offers:0:UnitPrice"] = "10", ["Shop:Offers:0:MaximumQuantity"] = "100",
            ["Shop:Offers:0:MaximumStackSize"] = "100", ["Shop:Offers:0:BindState"] = "Unbound"
        };
        assert(ShopCatalogConfiguration.Load(Config(settings)).Offers.Single() == Potion, "strict catalog loads valid buy offer");
        InvalidConfig(Config(new(settings) { ["Shop:Enabled"] = "truthy" }));
        InvalidConfig(Config(new(settings) { ["Shop:Unknown"] = "1" }));
        InvalidConfig(Config(new(settings) { ["Shop:Offers:0:UnitPrice"] = "1.0" }));
        InvalidConfig(Config(new(settings) { ["Shop:Offers:0:BindState"] = "0" }));
        InvalidConfig(Config(new(settings) { ["Shop:Offers:0:MaximumStackSize"] = "2" }));
        InvalidConfig(Config(new(settings) { ["Shop:Offers:2:OfferId"] = "gap" }));
        InvalidConfig(Config(new(settings) { ["Shop:Offers:0:OfferId:Nested"] = "bad" }));
    }
}
