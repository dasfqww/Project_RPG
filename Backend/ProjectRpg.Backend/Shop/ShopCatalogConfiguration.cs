using System.Globalization;
using Microsoft.Extensions.Configuration;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Shop;

/// <summary>Strict startup binding; omitted configuration disables the service without inventing merchandise.</summary>
public static class ShopCatalogConfiguration
{
    public static ShopCatalog Load(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        var section = configuration.GetSection("Shop");
        var fields = section.GetChildren().ToArray();
        if (section.Value is not null) throw Invalid(section.Path, "must be an object");
        if (fields.Length == 0) return new(false, "", "", "", [], []);
        ExactFields(section, ["Enabled", "ShopId", "DisplayName", "CurrencyCode", "Offers", "SaleRules"], false);
        bool enabled = Flag(section, "Enabled");
        if (!enabled && fields.All(f => f.Key.Equals("Enabled", StringComparison.OrdinalIgnoreCase)
                || (f.Key is "Offers" or "SaleRules") && f.Value is null && !f.GetChildren().Any()))
            return new(false, "", "", "", [], []);
        var offers = Entries(section.GetSection("Offers")).Select(entry =>
        {
            ExactFields(entry, ["OfferId", "DisplayName", "DefinitionType", "DefinitionName", "DefinitionVersion",
                "UnitPrice", "MaximumQuantity", "MaximumStackSize", "BindState"]);
            return new ShopOffer(Text(entry, "OfferId"), Text(entry, "DisplayName"), Text(entry, "DefinitionType"),
                Text(entry, "DefinitionName"), Integer(entry, "DefinitionVersion"), Money(entry, "UnitPrice"),
                Integer(entry, "MaximumQuantity"), Integer(entry, "MaximumStackSize"), Binding(entry, "BindState"));
        }).ToArray();
        var sales = Entries(section.GetSection("SaleRules")).Select(entry =>
        {
            ExactFields(entry, ["RuleId", "DisplayName", "DefinitionType", "DefinitionName", "DefinitionVersion",
                "UnitPrice", "MaximumQuantity", "MaximumStackSize", "AllowedBindStates"]);
            var bindings = Entries(entry.GetSection("AllowedBindStates"), true).Select(ValueBinding).ToArray();
            return new ShopSaleRule(Text(entry, "RuleId"), Text(entry, "DisplayName"), Text(entry, "DefinitionType"),
                Text(entry, "DefinitionName"), Integer(entry, "DefinitionVersion"), Money(entry, "UnitPrice"),
                Integer(entry, "MaximumQuantity"), Integer(entry, "MaximumStackSize"), Array.AsReadOnly(bindings));
        }).ToArray();
        try { return new(enabled, Text(section, "ShopId"), Text(section, "DisplayName"), Text(section, "CurrencyCode"), offers, sales); }
        catch (ShopRuleException error) { throw new InvalidOperationException("Shop contains invalid or duplicate catalog entries.", error); }
    }

    private static IConfigurationSection[] Entries(IConfigurationSection section, bool scalar = false)
    {
        if (section.Value is not null) throw Invalid(section.Path, "must be an indexed list");
        var entries = section.GetChildren().ToArray();
        if (entries.Length > ShopRules.MaximumCatalogEntries) throw Invalid(section.Path, "has too many entries");
        for (int i = 0; i < entries.Length; ++i)
            if (entries[i].Key != i.ToString(CultureInfo.InvariantCulture)
                || (scalar ? entries[i].Value is null || entries[i].GetChildren().Any() : entries[i].Value is not null))
                throw Invalid(section.Path, "requires consecutive indices and the expected value shape");
        return entries;
    }
    private static void ExactFields(IConfigurationSection section, string[] allowed, bool requireAll = true)
    {
        var fields = section.GetChildren().ToArray();
        if (section.Value is not null || fields.Any(f => !allowed.Contains(f.Key, StringComparer.OrdinalIgnoreCase))
            || requireAll && fields.Length != allowed.Length)
            throw Invalid(section.Path, "contains missing or unsupported fields");
    }
    private static string Text(IConfigurationSection section, string name)
    {
        var field = section.GetSection(name);
        if (string.IsNullOrWhiteSpace(field.Value) || field.Value != field.Value.Trim() || field.GetChildren().Any())
            throw Invalid(field.Path, "requires a nonempty scalar without surrounding whitespace");
        return field.Value;
    }
    private static int Integer(IConfigurationSection section, string name) =>
        int.TryParse(Text(section, name), NumberStyles.None, CultureInfo.InvariantCulture, out int n) && n > 0
            ? n : throw Invalid(section.Path + ":" + name, "requires a positive int32");
    private static long Money(IConfigurationSection section, string name) =>
        long.TryParse(Text(section, name), NumberStyles.None, CultureInfo.InvariantCulture, out long n) && n > 0
            ? n : throw Invalid(section.Path + ":" + name, "requires a positive int64");
    private static bool Flag(IConfigurationSection section, string name) => Text(section, name) switch
    {
        var s when s.Equals("true", StringComparison.OrdinalIgnoreCase) => true,
        var s when s.Equals("false", StringComparison.OrdinalIgnoreCase) => false,
        _ => throw Invalid(section.Path + ":" + name, "requires true or false")
    };
    private static ItemBindState Binding(IConfigurationSection section, string name) => ValueBinding(section.GetSection(name));
    private static ItemBindState ValueBinding(IConfigurationSection field)
    {
        if (field.Value is null || field.GetChildren().Any()) throw Invalid(field.Path, "requires a named binding state");
        return field.Value switch
        {
            "Unbound" => ItemBindState.Unbound,
            "BindOnEquip" => ItemBindState.BindOnEquip,
            "CharacterBound" => ItemBindState.CharacterBound,
            "AccountBound" => ItemBindState.AccountBound,
            _ => throw Invalid(field.Path, "requires Unbound, BindOnEquip, CharacterBound or AccountBound")
        };
    }
    private static InvalidOperationException Invalid(string path, string detail) => new($"{path} {detail}.");
}
