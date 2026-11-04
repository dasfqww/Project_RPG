using System.Text.Json;
using System.Text.Json.Serialization;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Evidence;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

internal static class EvidenceMapping
{
    internal static readonly Failure Missing = new(FailureKind.NotFound, "ops.evidence_not_found");
    internal static readonly JsonSerializerOptions StoredJson = CreateJson();
    private static JsonSerializerOptions CreateJson()
    {
        var json = new JsonSerializerOptions(JsonSerializerDefaults.Web);
        json.Converters.Add(new JsonStringEnumConverter());
        return json;
    }
    internal static EvidenceItem Currency(CurrencyTransactionResult r) => new(EvidenceSourceKind.CurrencyReceipt,
        r.RequestId, r.CommittedAt, r.CharacterId, "Character", r.CharacterId.ToString("D"), null, r.RequestId, [],
        EvidenceStatus.Delivered, EvidenceLinkReason.DirectReceipt, r.Operation,
        Array.AsReadOnly(r.Changes.Take(16).Select(c => new CurrencyEvidence(c.CurrencyCode, c.Scope.ToString(), c.OwnerId,
            c.Delta, c.PreviousBalance, c.NewBalance, c.Revision)).ToArray()), DetailsTruncated: r.Changes.Count > 16);
    internal static bool Matches(ItemRepositoryCommitResult r, CharacterHeader c) => ActorMatches(r.Actor, c)
        || r.Records.Any(i => OwnerMatches(i.Owner, c));
    internal static bool ActorMatches(ItemOwnerRef owner, CharacterHeader c) => OwnerMatches(owner, c);
    internal static bool OwnerMatches(ItemOwnerRef owner, CharacterHeader c) =>
        owner.Type == ItemOwnerType.Character && owner.OwnerId == c.CharacterId.ToString("D")
        || owner.Type == ItemOwnerType.Account && owner.OwnerId == c.AccountId;
    internal static EvidenceItem Item(ItemRepositoryCommitResult r, CharacterHeader c)
    {
        bool actor = ActorMatches(r.Actor, c);
        // A result-owner match does not expose other participants' item IDs.
        Guid[] ids = r.Records.Where(i => actor || OwnerMatches(i.Owner, c)).Select(i => i.ItemId).Distinct().Take(65).ToArray();
        return new(EvidenceSourceKind.ItemReceipt, r.RequestId, r.CommittedAt, c.CharacterId,
            actor ? r.Actor.Type.ToString() : "CharacterOrAccount", actor ? r.Actor.OwnerId : c.CharacterId.ToString("D"),
            null, r.RequestId, Array.AsReadOnly(ids.Take(64).ToArray()), EvidenceStatus.Delivered,
            actor && r.Actor.Type == ItemOwnerType.Character ? EvidenceLinkReason.DirectReceipt : EvidenceLinkReason.OwnerScope,
            r.Operation, [], DetailsTruncated: ids.Length > 64);
    }
    internal static EvidenceItem Settlement(Guid session, Guid character, DateTimeOffset updated, string state) =>
        new(EvidenceSourceKind.Settlement, session, updated, character, "Character", character.ToString("D"), session, null, [],
            state is "Pending" or "Processing" ? EvidenceStatus.Pending : state == "Failed" ? EvidenceStatus.Failed : EvidenceStatus.Unknown,
            EvidenceLinkReason.SessionMembership, "DungeonSettlement", [], state);
    internal static bool InPage(EvidenceItem item, EvidenceQuery query, EvidenceSourceKind source) =>
        item.OccurredAtUtc >= query.FromUtc && item.OccurredAtUtc < query.ToUtc && (query.After?.For(source) is not { } after
            || item.OccurredAtUtc < after.Timestamp || item.OccurredAtUtc == after.Timestamp && item.SourceKey.CompareTo(after.Id) < 0);
}
