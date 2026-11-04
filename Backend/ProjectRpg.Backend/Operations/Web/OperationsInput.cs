using System.Globalization;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Operations.Web;

public static class OperationsInput
{
    public static bool KnownQuery(HttpContext context, params string[] allowed) =>
        context.Request.Query.All(p => allowed.Contains(p.Key, StringComparer.Ordinal) && p.Value.Count == 1
            && p.Value[0] is { } v && v.Length <= (p.Key == "cursor" ? 8192 : 128));
    public static bool Limit(HttpContext context, int defaultValue, int maximum, out int value)
    {
        string? raw = context.Request.Query["limit"];
        value = defaultValue;
        return string.IsNullOrEmpty(raw) || int.TryParse(raw, NumberStyles.None, CultureInfo.InvariantCulture, out value)
            && value >= 1 && value <= maximum;
    }
    public static bool Id(string? raw, out Guid id) => CharacterLookup.ValidGuid(raw, out id);
    public static bool Kind(HttpContext context, out CharacterLookupKind kind) =>
        Enum.TryParse(context.Request.Query["kind"], ignoreCase: false, out kind) && Enum.IsDefined(kind)
        && context.Request.Query["kind"] == kind.ToString();
    public static bool Container(HttpContext context, out ProjectRpg.Operations.Core.Inventory.InventoryContainer container)
    {
        string raw = context.Request.Query["container"].ToString();
        if (raw.Length == 0) raw = "Inventory";
        return Enum.TryParse(raw, ignoreCase: false, out container) && Enum.IsDefined(container) && raw == container.ToString();
    }
    public static bool Logs(HttpContext context, TimeProvider clock, string source,
        out OperationLogQuery query)
    {
        DateTimeOffset to = clock.GetUtcNow(), from = to.AddHours(-1);
        string rawFrom = context.Request.Query["fromUtc"].ToString(), rawTo = context.Request.Query["toUtc"].ToString();
        bool valid = (rawFrom.Length == 0 || Utc(rawFrom, out from)) && (rawTo.Length == 0 || Utc(rawTo, out to))
            && KnownQuery(context, [.. OperationLogQuery.IdFields, "eventName", "level", "fromUtc", "toUtc", "sourceId", "limit", "cursor"])
            && Limit(context, 100, 200, out _);
        int limit = Limit(context, 100, 200, out int parsed) ? parsed : 100;
        Dictionary<string,string> filters = context.Request.Query
            .Where(p => OperationLogQuery.IdFields.Contains(p.Key) || p.Key is "eventName" or "level")
            .Where(p => !string.IsNullOrEmpty(p.Value.ToString())).ToDictionary(p => p.Key, p => p.Value.ToString(), StringComparer.Ordinal);
        query = new(from, to, context.Request.Query["sourceId"].FirstOrDefault() ?? source, filters, limit);
        return valid && query.IsValid;
    }
    public static string LogKey(OperationLogQuery query) => "logs\n" + query.SourceId + "\n"
        + query.FromUtc.ToString("O", CultureInfo.InvariantCulture) + "\n" + query.ToUtc.ToString("O", CultureInfo.InvariantCulture)
        + "\n" + query.Limit + "\n" + string.Join("\n", query.Filters.OrderBy(p => p.Key, StringComparer.Ordinal).Select(p => p.Key + "=" + p.Value));
    private static bool Utc(string raw, out DateTimeOffset value) =>
        DateTimeOffset.TryParse(raw, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out value) && raw.EndsWith('Z');
}
