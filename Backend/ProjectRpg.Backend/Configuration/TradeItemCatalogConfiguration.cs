using System.Globalization;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Configuration;

/// <summary>Loads one immutable, shared direct-trade/auction allowlist at startup.</summary>
public static class TradeItemCatalogConfiguration
{
    private static readonly HashSet<string> Fields = new(StringComparer.OrdinalIgnoreCase)
    {
        "DefinitionType", "DefinitionName", "DefinitionVersion",
        "AllowDirect", "AllowAuction", "MaximumStackSize"
    };

    public static TradeItemPolicy Load(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        var section = configuration.GetSection("Trading:Items");
        if (section.Value is not null) throw Invalid(section.Path, "must be an indexed list");
        var entries = section.GetChildren().ToArray();
        var definitions = new List<TradeDefinitionPolicy>(entries.Length);
        for (int index = 0; index < entries.Length; index++)
        {
            var entry = entries[index];
            if (entry.Key != index.ToString(CultureInfo.InvariantCulture) || entry.Value is not null)
                throw Invalid(section.Path, "must use consecutive indices starting at zero");
            var fields = entry.GetChildren().ToArray();
            if (fields.Length != Fields.Count || fields.Any(field => !Fields.Contains(field.Key)
                || field.Value is null || field.GetChildren().Any()))
                throw Invalid(entry.Path, "requires exactly DefinitionType, DefinitionName, DefinitionVersion, AllowDirect, AllowAuction and MaximumStackSize scalar fields");
            definitions.Add(new(Text(entry, "DefinitionType"), Text(entry, "DefinitionName"),
                PositiveInteger(entry, "DefinitionVersion"), Flag(entry, "AllowDirect"),
                Flag(entry, "AllowAuction"), PositiveInteger(entry, "MaximumStackSize")));
        }
        try { return new TradeItemPolicy(definitions); }
        catch (TradeRuleException error)
        {
            throw new InvalidOperationException("Trading:Items contains an invalid or duplicate definition identity.", error);
        }
    }

    private static string Text(IConfigurationSection entry, string name)
    {
        string value = entry[name]!;
        if (string.IsNullOrWhiteSpace(value) || value != value.Trim())
            throw Invalid(entry.Path + ":" + name, "must be a nonempty identity without surrounding whitespace");
        return value;
    }

    private static int PositiveInteger(IConfigurationSection entry, string name) =>
        int.TryParse(entry[name], NumberStyles.None, CultureInfo.InvariantCulture, out int value) && value > 0
            ? value : throw Invalid(entry.Path + ":" + name, "must be a positive int32");

    private static bool Flag(IConfigurationSection entry, string name)
    {
        string value = entry[name]!;
        if (value.Equals("true", StringComparison.OrdinalIgnoreCase)) return true;
        if (value.Equals("false", StringComparison.OrdinalIgnoreCase)) return false;
        throw Invalid(entry.Path + ":" + name, "must explicitly be true or false");
    }

    private static InvalidOperationException Invalid(string path, string rule) => new($"{path} {rule}.");
}
