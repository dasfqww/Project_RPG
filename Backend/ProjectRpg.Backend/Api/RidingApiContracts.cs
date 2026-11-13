using System.Globalization;
using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Api;

public static class RidingApiContracts
{
    public static RidingRegistrationCommand Command(JsonElement body)
    {
        if (body.ValueKind != JsonValueKind.Object) throw Invalid();
        HashSet<string> names = [];
        foreach (var property in body.EnumerateObject())
            if (!names.Add(property.Name) || property.Name is not ("requestId" or "itemId" or "expectedItemRevision" or "catalogVersion")) throw Invalid();
        if (names.Count != 4) throw Invalid();
        var request = body.GetProperty("requestId"); var item = body.GetProperty("itemId");
        var version = body.GetProperty("catalogVersion"); var expected = body.GetProperty("expectedItemRevision");
        if (request.ValueKind != JsonValueKind.String || !Guid.TryParseExact(request.GetString(), "D", out var requestId) || requestId == Guid.Empty
            || item.ValueKind != JsonValueKind.String || !Guid.TryParseExact(item.GetString(), "D", out var itemId) || itemId == Guid.Empty
            || version.ValueKind != JsonValueKind.Number || !version.TryGetInt32(out var catalogVersion) || catalogVersion <= 0
            || expected.ValueKind != JsonValueKind.String) throw Invalid();
        string text = expected.GetString()!;
        if (text.Length > 19 || !long.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out long revision)
            || revision is <= 0 or long.MaxValue || text != Number(revision)) throw Invalid();
        return new(requestId, itemId, revision, catalogVersion);
    }
    public static object Result(RidingRegistrationResult result) => new
    { outcome = result.Replayed ? "AlreadyApplied" : result.Receipt.Outcome.ToString(), receipt = Receipt(result.Receipt) };
    public static object Receipt(RidingRegistrationReceipt r) => new
    {
        r.RequestId, r.Operation, r.CharacterId, r.RosterId, outcome = r.Outcome.ToString(), r.MountId,
        rosterRevision = Number(r.RosterRevision), r.CommittedAtUtc,
        itemReceipt = r.ItemReceipt is null ? null : new
        {
            r.ItemReceipt.RequestId, r.ItemReceipt.AffectedQuantity,
            records = r.ItemReceipt.Records.Select(Item).ToArray()
        }
    };
    // Explicit projection prevents accidental int64 JSON numbers or publication of internal command fingerprints.
    private static object Item(ItemRecord r) => new
    {
        r.ItemId, r.DefinitionType, r.DefinitionName, r.DefinitionVersion, revision = Number(r.Revision),
        lifecycleState = r.LifecycleState.ToString(), owner = new { type = r.Owner.Type.ToString(), r.Owner.OwnerId },
        location = new { containerType = r.Location.ContainerType.ToString(), r.Location.ContainerId, r.Location.SlotIndex },
        r.State, metadata = new { bindState = r.Metadata.BindState.ToString(), r.Metadata.Durability,
            r.Metadata.ExpiresAtUtc, r.Metadata.CreationSource, r.Metadata.IsLocked }
    };
    public static string Number(long number) => number.ToString(CultureInfo.InvariantCulture);
    public static RidingRuleException Invalid() => new(RidingFailure.InvalidRequest);
}
