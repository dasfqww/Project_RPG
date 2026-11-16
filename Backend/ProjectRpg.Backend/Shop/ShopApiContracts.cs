using System.Globalization;
using System.Text.Json;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Shop;

public static class ShopApiContracts
{
    public static string Number(long value) => value.ToString(CultureInfo.InvariantCulture);
    public static object Receipt(ShopReceipt r) => new { r.RequestId, characterId = r.Actor.CharacterId,
        r.ShopId, r.PolicyTag, r.Action, r.OfferId, r.ItemId, r.Quantity, unitPrice = Number(r.UnitPrice),
        totalPrice = Number(r.TotalPrice), r.CurrencyCode, balanceBefore = Number(r.BalanceBefore),
        balanceAfter = Number(r.BalanceAfter), committedAtUtc = r.CommittedAt };
    public static object Rejection(ShopRejectedException rejected)
    {
        var c = rejected.Command;
        return new { rejected = true, error = rejected.Code, c.RequestId, characterId = c.Actor.CharacterId,
            c.ShopId, c.PolicyTag, c.Action, c.OfferId, c.ItemId, c.Quantity, expectedUnitPrice = Number(c.ExpectedUnitPrice),
            expectedItemRevision = c.ExpectedItemRevision is { } revision ? Number(revision) : null,
            rejectedAtUtc = rejected.RejectedAt };
    }
    public static object Assets(ShopAssets r) => new { r.RequestId, r.CharacterId, r.ObservedAt, isComplete = true,
        wallet = EconomyApiContractMapper.ToContract(r.Wallet), items = r.Items.Select(ItemApiContractMapper.ToContract).ToArray() };

    public static object Snapshot(ShopCatalog catalog, ShopInventorySnapshot snapshot)
    {
        var inventory = snapshot.Items.Where(i => i.Location.ContainerType == ItemContainerType.Inventory).ToArray();
        if (inventory.Any(i => !ItemRecordRules.TryValidateRecord(i, out _) || !InventoryBagLayout.CanStore(i))
            || inventory.Select(i => i.Location.SlotIndex).Distinct().Count() != inventory.Length)
            throw new ShopRuleException("shop_assets_unavailable");
        return new { catalog.ShopId, catalog.DisplayName, catalog.PolicyTag, catalog.CurrencyCode,
            balance = Number(snapshot.Balance.Balance), availableBalance = Number(snapshot.AvailableBalance),
            inventoryFreeSlots = Enumerable.Range(0, 3).Select(tab => 80 - inventory.Count(i => i.Location.SlotIndex / 80 == tab)).ToArray(),
            offers = catalog.Offers.Select(o => new { o.OfferId, o.DisplayName, o.DefinitionType, o.DefinitionName,
                o.DefinitionVersion, unitPrice = Number(o.UnitPrice), o.MaximumQuantity, o.MaximumStackSize,
                maximumPurchasable = MaximumPurchase(o, catalog, snapshot, inventory) }).ToArray(),
            saleItems = snapshot.Items.Where(i => i.Location.ContainerType is ItemContainerType.Inventory or ItemContainerType.Equipment)
                .OrderBy(i => i.Location.ContainerType).ThenBy(i => i.Location.SlotIndex).Select(i => Sale(i, catalog, snapshot)).ToArray() };
    }
    private static int MaximumPurchase(ShopOffer offer, ShopCatalog catalog, ShopInventorySnapshot snapshot, ItemRecord[] inventory)
    {
        int low = 0, high = offer.MaximumQuantity;
        var command = new ShopCommand(Guid.NewGuid(), snapshot.Actor, catalog.ShopId, catalog.PolicyTag,
            "buy", offer.OfferId, 1, offer.UnitPrice);
        while (low < high)
        {
            int middle = low + (high - low + 1) / 2;
            try
            {
                var plan = ShopPlanner.Plan(command with { Quantity = middle }, catalog, inventory, snapshot.ReservedItems,
                    snapshot.Balance.Balance, snapshot.AvailableBalance, snapshot.Balance.Definition.MaxBalance, snapshot.ObservedAt);
                if (snapshot.Items.Count + plan.Mutations.Count(m => m.ExpectedRevision == 0) > 500) high = middle - 1;
                else low = middle;
            }
            catch (ShopRuleException e) when (e.Code is "shop_insufficient_balance" or "shop_inventory_full" or "shop_quantity_limit")
            { high = middle - 1; }
        }
        return low;
    }
    private static object Sale(ItemRecord item, ShopCatalog catalog, ShopInventorySnapshot snapshot)
    {
        var rule = catalog.SaleRuleFor(item);
        string? reason = rule is null ? "shop_sale_not_allowed" : ShopPlanner.SaleBlockReason(item, rule, snapshot.Actor,
            snapshot.ReservedItems.Contains(item.ItemId), snapshot.ObservedAt);
        int maximum = rule is null ? 0 : Math.Min(item.State.Quantity, rule.MaximumQuantity);
        if (rule is not null && reason is null)
        {
            maximum = (int)Math.Min(maximum, Math.Max(0, snapshot.Balance.Definition.MaxBalance - snapshot.Balance.Balance) / rule.UnitPrice);
            if (maximum == 0) reason = "shop_wallet_limit";
        }
        return new { itemId = item.ItemId, revision = Number(item.Revision), ruleId = rule?.RuleId ?? "",
            displayName = rule?.DisplayName ?? catalog.Offers.FirstOrDefault(o => o.DefinitionType == item.DefinitionType
                && o.DefinitionName == item.DefinitionName && o.DefinitionVersion == item.DefinitionVersion)?.DisplayName ?? "판매 제외 물품",
            quantity = item.State.Quantity, maximumQuantity = reason is null ? maximum : 0,
            unitPrice = Number(rule?.UnitPrice ?? 0), canSell = reason is null, reason = reason ?? "" };
    }

    public static ShopCommand Command(JsonElement root, ShopActor actor)
    {
        if (root.ValueKind != JsonValueKind.Object) throw Invalid();
        try { TradeApiContracts.RejectDuplicateProperties(root); }
        catch (ProjectRpg.Backend.Domain.Trading.TradeRuleException) { throw Invalid(); }
        string[] required = ["requestId", "shopId", "policyTag", "action", "offerId", "quantity", "expectedUnitPrice"];
        if (required.Any(name => !root.TryGetProperty(name, out _))
            || root.EnumerateObject().Any(p => !required.Contains(p.Name, StringComparer.Ordinal)
                && p.Name is not ("itemId" or "expectedItemRevision"))) throw Invalid();
        string Text(string name) => root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()! : throw Invalid();
        if (!Guid.TryParseExact(Text("requestId"), "D", out var request) || !root.GetProperty("quantity").TryGetInt32(out int quantity)) throw Invalid();
        Guid? item = null; long? revision = null;
        if (root.TryGetProperty("itemId", out var id) && id.ValueKind != JsonValueKind.Null)
        { if (id.ValueKind != JsonValueKind.String || !Guid.TryParseExact(id.GetString(), "D", out var parsed)) throw Invalid(); item = parsed; }
        if (root.TryGetProperty("expectedItemRevision", out var rev) && rev.ValueKind != JsonValueKind.Null) revision = Integer(Text("expectedItemRevision"));
        var result = new ShopCommand(request, actor, Text("shopId"), Text("policyTag"), Text("action"), Text("offerId"),
            quantity, Integer(Text("expectedUnitPrice")), item, revision);
        ShopRules.Validate(result); return result;
    }
    private static long Integer(string text) => text.Length <= 19 && long.TryParse(text, NumberStyles.None,
        CultureInfo.InvariantCulture, out long value) && value > 0 && text == Number(value) ? value : throw Invalid();
    private static ShopRuleException Invalid() => new("invalid_shop_command");
}
