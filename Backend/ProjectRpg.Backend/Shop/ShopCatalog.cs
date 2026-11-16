using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Shop;

/// <summary>Immutable, server-owned prices and item identities. Never built from a purchase request.</summary>
public sealed class ShopCatalog
{
    public bool Enabled { get; }
    public string ShopId { get; }
    public string DisplayName { get; }
    public string CurrencyCode { get; }
    public IReadOnlyList<ShopOffer> Offers { get; }
    public IReadOnlyList<ShopSaleRule> SaleRules { get; }
    public string PolicyTag { get; }

    public ShopCatalog(bool enabled, string shopId, string displayName, string currencyCode,
        IEnumerable<ShopOffer> offers, IEnumerable<ShopSaleRule> saleRules)
    {
        ArgumentNullException.ThrowIfNull(offers); ArgumentNullException.ThrowIfNull(saleRules);
        var buys = offers.Take(ShopRules.MaximumCatalogEntries + 1).ToArray();
        var sales = saleRules.Take(ShopRules.MaximumCatalogEntries + 1).ToArray();
        // The entirely absent section is disabled, but a partially configured catalog still fails closed.
        bool absent = !enabled && shopId == "" && displayName == "" && currencyCode == ""
            && buys.Length == 0 && sales.Length == 0;
        if (!absent && (!ShopRules.ValidIdentity(shopId, 64) || !ShopRules.ValidDisplay(displayName)
            || !ShopRules.ValidIdentity(currencyCode, 64))) throw Invalid();
        if (enabled && buys.Length == 0 || buys.Length > ShopRules.MaximumCatalogEntries
            || sales.Length > ShopRules.MaximumCatalogEntries) throw Invalid();
        foreach (var b in buys)
            if (b is null || !ShopRules.ValidIdentity(b.OfferId, 64) || !ShopRules.ValidDisplay(b.DisplayName)
                || !Definition(b.DefinitionType, b.DefinitionName, b.DefinitionVersion)
                || !b.DefinitionName.StartsWith("GameItem.Consume.", StringComparison.Ordinal)
                || !Limits(b.UnitPrice, b.MaximumQuantity, b.MaximumStackSize)
                || !Enum.IsDefined(b.BindState)) throw Invalid();
        foreach (var s in sales)
            if (s is null || !ShopRules.ValidIdentity(s.RuleId, 64) || !ShopRules.ValidDisplay(s.DisplayName)
                || !Definition(s.DefinitionType, s.DefinitionName, s.DefinitionVersion)
                || InventoryBagLayout.Tab(s.DefinitionName) != 2 || ProtectedName(s.DefinitionName)
                || !Limits(s.UnitPrice, s.MaximumQuantity, s.MaximumStackSize)
                || s.AllowedBindStates is null || s.AllowedBindStates.Count is < 1 or > 4
                || s.AllowedBindStates.Any(b => !Enum.IsDefined(b))
                || s.AllowedBindStates.Distinct().Count() != s.AllowedBindStates.Count) throw Invalid();
        if (buys.Select(b => b.OfferId).Distinct(StringComparer.Ordinal).Count() != buys.Length
            || sales.Select(s => s.RuleId).Distinct(StringComparer.Ordinal).Count() != sales.Length
            || buys.Select(b => (b.DefinitionType, b.DefinitionName, b.DefinitionVersion)).Distinct().Count() != buys.Length
            || sales.Select(s => (s.DefinitionType, s.DefinitionName, s.DefinitionVersion)).Distinct().Count() != sales.Length)
            throw Invalid();
        Enabled = enabled; ShopId = shopId; DisplayName = displayName; CurrencyCode = currencyCode;
        Offers = Array.AsReadOnly(buys.OrderBy(b => b.OfferId, StringComparer.Ordinal).ToArray());
        SaleRules = Array.AsReadOnly(sales.OrderBy(s => s.RuleId, StringComparer.Ordinal).Select(s => s with
        { AllowedBindStates = Array.AsReadOnly(s.AllowedBindStates.Order().ToArray()) }).ToArray());
        PolicyTag = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new
        { Version = 1, Enabled, ShopId, DisplayName, CurrencyCode, Offers, SaleRules }))));
    }

    public ShopOffer Offer(string id) => Offers.SingleOrDefault(o => o.OfferId == id)
        ?? throw new ShopRuleException("shop_offer_unavailable");
    public ShopSaleRule SaleRule(string id) => SaleRules.SingleOrDefault(o => o.RuleId == id)
        ?? throw new ShopRuleException("shop_sale_not_allowed");
    public ShopSaleRule? SaleRuleFor(ItemRecord item) => SaleRules.SingleOrDefault(o =>
        o.DefinitionType == item.DefinitionType && o.DefinitionName == item.DefinitionName
        && o.DefinitionVersion == item.DefinitionVersion);

    internal static bool ProtectedName(string name) => name.Split('.').Any(p =>
        p.Equals("Quest", StringComparison.OrdinalIgnoreCase) || p.Equals("Story", StringComparison.OrdinalIgnoreCase)
        || p.Equals("Progression", StringComparison.OrdinalIgnoreCase));
    private static bool Definition(string type, string name, int version) => ShopRules.ValidIdentity(type, 64)
        && ShopRules.ValidIdentity(name) && name.StartsWith("GameItem.", StringComparison.Ordinal) && version > 0;
    private static bool Limits(long price, int maximum, int stack) => price > 0
        && maximum is >= 1 and <= ShopRules.MaximumOrderQuantity && stack >= maximum
        && stack <= 1_000_000 && price <= long.MaxValue / maximum;
    private static ShopRuleException Invalid() => new("invalid_shop_catalog");
}
