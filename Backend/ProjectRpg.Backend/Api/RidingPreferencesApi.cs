using System.Text.Json;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Api;

public static partial class RidingApiEndpoints
{
    private static object PreferencesContract(RidingPreferences p) => new
    { p.SelectedMountId, p.FavoriteMountIds, shortcut = new { mode = p.Shortcut.Mode.ToString(), p.Shortcut.MountId } };
    private static object PreferencesReceiptContract(RidingPreferencesReceipt r) => new
    {
        r.RequestId, r.Operation, r.CharacterId, r.RosterId, outcome = "PreferencesSaved", preferencesRevision = RidingApiContracts.Number(r.Preferences.Revision),
        preferences = PreferencesContract(r.Preferences), r.CommittedAtUtc
    };
    private static object CommandReceiptContract(RidingCommandReceipt r) => r.Preferences is { } p ? PreferencesReceiptContract(p)
        : RidingApiContracts.Receipt(r.Registration ?? throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt));
    private static void MapRidingPreferencesApi(WebApplication app)
    {
        app.MapPut(Root + "/preferences", (Guid characterId, HttpContext c, IRidingPreferencesStore store, RidingRuntime runtime, RidingRequestLimiter limiter, CancellationToken ct) =>
            Run(c, limiter, true, async account =>
            {
                if (!c.Request.HasJsonContentType()) return Results.StatusCode(415);
                if (c.Request.ContentLength > MaximumBodyBytes) return Results.StatusCode(413);
                using var bytes = new MemoryStream(); byte[] buffer = new byte[2048];
                while (true)
                {
                    int remaining = MaximumBodyBytes - (int)bytes.Length;
                    int count = await c.Request.Body.ReadAsync(buffer.AsMemory(0, Math.Min(buffer.Length, remaining + 1)), ct);
                    if (count == 0) break; if (count > remaining) return Results.StatusCode(413); bytes.Write(buffer, 0, count);
                }
                RidingPreferencesCommand command;
                try { using var json = JsonDocument.Parse(bytes.ToArray(), new JsonDocumentOptions { MaxDepth = 8 }); command = PreferencesCommand(json.RootElement); }
                catch (JsonException) { throw RidingApiContracts.Invalid(); }
                RidingPreferencesResult result;
                if (await store.GetRidingCommandReceiptAsync(account, characterId, command.RequestId, ct) is { } old)
                {
                    if (old.Preferences is not { } receipt || receipt.Fingerprint != RidingPreferencesRules.Fingerprint(new(characterId, receipt.RosterId), command))
                        throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
                    result = new(true, receipt);
                }
                else
                {
                    if (runtime.Catalog is null) return Results.NotFound(new { error = "RidingDisabled" });
                    result = await store.SaveRidingPreferencesAsync(account, characterId, command, runtime.Catalog, ct);
                }
                return new { outcome = result.Replayed ? "AlreadyApplied" : "PreferencesSaved", receipt = PreferencesReceiptContract(result.Receipt) };
            }));
    }
    private static void Shape(JsonElement value, params string[] required)
    {
        if (value.ValueKind != JsonValueKind.Object) throw RidingApiContracts.Invalid(); HashSet<string> seen = [];
        foreach (var p in value.EnumerateObject()) if (!seen.Add(p.Name) || !required.Contains(p.Name, StringComparer.Ordinal)) throw RidingApiContracts.Invalid();
        if (seen.Count != required.Length) throw RidingApiContracts.Invalid();
    }
    private static string? OptionalString(JsonElement value) => value.ValueKind switch
    { JsonValueKind.Null => null, JsonValueKind.String => value.GetString(), _ => throw RidingApiContracts.Invalid() };
    private static RidingPreferencesCommand PreferencesCommand(JsonElement value)
    {
        Shape(value, "requestId", "expectedRevision", "selectedMountId", "favoriteMountIds", "shortcut");
        var request = value.GetProperty("requestId"); var expected = value.GetProperty("expectedRevision"); var favorites = value.GetProperty("favoriteMountIds");
        if (request.ValueKind != JsonValueKind.String || !Guid.TryParseExact(request.GetString(), "D", out Guid id) || id == Guid.Empty
            || expected.ValueKind != JsonValueKind.String || favorites.ValueKind != JsonValueKind.Array || favorites.GetArrayLength() > 32) throw RidingApiContracts.Invalid();
        string text = expected.GetString()!;
        if (text.Length > 19 || !long.TryParse(text, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out long revision)
            || revision is < 0 or long.MaxValue || RidingApiContracts.Number(revision) != text) throw RidingApiContracts.Invalid();
        var shortcut = value.GetProperty("shortcut"); Shape(shortcut, "mode", "mountId");
        var mode = OptionalString(shortcut.GetProperty("mode")) switch
        { "Selected" => RidingShortcutMode.Selected, "Specific" => RidingShortcutMode.Specific, "RandomFavorites" => RidingShortcutMode.RandomFavorites, _ => throw RidingApiContracts.Invalid() };
        var list = favorites.EnumerateArray().Select(v => OptionalString(v) ?? throw RidingApiContracts.Invalid()).ToArray();
        return RidingPreferencesRules.Normalize(new(id, revision, OptionalString(value.GetProperty("selectedMountId")), list, new(mode, OptionalString(shortcut.GetProperty("mountId")))));
    }
}
