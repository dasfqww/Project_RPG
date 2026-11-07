using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Api;

public sealed record TradeItemRequest(Guid ItemId, string? ExpectedRevision, int? Quantity);
public sealed record TradeOfferRequest(IReadOnlyList<TradeItemRequest?>? Items, string? CurrencyAmount);
public sealed record TradeCommandRequest(Guid RequestId, string? ExpectedRevision = null,
    string? ExpectedOfferRevision = null, Guid? InviteeCharacterId = null, TradeOfferRequest? Offer = null);

public static class TradeApiContracts
{
    internal static readonly JsonSerializerOptions InputOptions = new(JsonSerializerDefaults.Web)
    {
        PropertyNameCaseInsensitive = false, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        NumberHandling = JsonNumberHandling.Strict, MaxDepth = 12
    };

    internal static (long Revision, long OfferRevision, TradeOffer? Offer) Parse(TradeCommandRequest request, TradeCommandKind kind)
    {
        if (request.RequestId == Guid.Empty) throw Invalid();
        if (kind == TradeCommandKind.Invite)
        {
            if (request.InviteeCharacterId is null || request.InviteeCharacterId == Guid.Empty || request.ExpectedRevision is not null
                || request.ExpectedOfferRevision is not null || request.Offer is not null) throw Invalid();
            return (0, 0, null);
        }
        long revision = Integer(request.ExpectedRevision, true);
        if (request.InviteeCharacterId is not null) throw Invalid();
        long offerRevision = kind is TradeCommandKind.Ready or TradeCommandKind.Confirm
            ? Integer(request.ExpectedOfferRevision, true) : 0;
        if (kind is not (TradeCommandKind.Ready or TradeCommandKind.Confirm) && request.ExpectedOfferRevision is not null) throw Invalid();
        if (kind != TradeCommandKind.ReplaceOffer)
        {
            if (request.Offer is not null) throw Invalid();
            return (revision, offerRevision, null);
        }
        if (request.Offer?.Items is null || request.Offer.Items.Count > TradeOffer.MaximumItemCount) throw Invalid();
        var amount = Integer(request.Offer.CurrencyAmount, false);
        List<TradeOfferedItem> items = [];
        foreach (var item in request.Offer.Items)
        {
            if (item?.Quantity is not int quantity) throw Invalid();
            items.Add(new(item.ItemId, Integer(item.ExpectedRevision, true), quantity));
        }
        return (revision, 0, new TradeOffer(items, amount));
    }

    private static long Integer(string? value, bool revision)
    {
        if (value is null || value.Length > 19 || !long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var n)
            || n < 0 || (revision && (n == 0 || n == long.MaxValue)) || value != n.ToString(CultureInfo.InvariantCulture)) throw Invalid();
        return n;
    }
    private static TradeRuleException Invalid() => new("invalid_trade_command");

    public static object Result(TradeSessionResult result) => new
    { result.RequestId, session = Session(result.Session), result.Rejection, result.CommittedAt };

    // Never serialize internal participants, wallet owners, hold IDs or the settlement plan to players.
    public static object Session(TradeStoredSession session)
    {
        var s = session.State;
        return new
        {
            s.TradeId, initiatorCharacterId = s.Initiator.CharacterId, inviteeCharacterId = s.Invitee.CharacterId,
            s.CurrencyCode, revision = Text(s.Revision), offerRevision = Text(s.OfferRevision), s.ExpiresAt,
            status = session.Completed ? "Completed" : s.Status.ToString(), session.Completed,
            initiatorOffer = Offer(s.InitiatorOffer), inviteeOffer = Offer(s.InviteeOffer),
            s.InitiatorReady, s.InviteeReady, s.InitiatorConfirmed, s.InviteeConfirmed
        };
    }
    private static object Offer(TradeOffer offer) => new
    {
        currencyAmount = Text(offer.CurrencyAmount),
        items = offer.Items.Select(i => new { i.ItemId, expectedRevision = Text(i.ExpectedRevision), i.Quantity }).ToArray()
    };
    private static string Text(long n) => n.ToString(CultureInfo.InvariantCulture);

    public static object Preview(TradePreview preview)
    {
        object[] Items(IReadOnlyList<TradePreviewItem> items) => items.Select(i => (object)new
        {
            i.ItemId, expectedRevision = Text(i.ExpectedRevision), i.Quantity,
            availability = i.Details is null ? "Unavailable" : "Available",
            details = i.Details is not { } d ? null : new
            {
                d.DefinitionType, d.DefinitionName, d.DefinitionVersion, d.InstanceTags, d.StatValues,
                d.EquipmentProgression, bindState = d.BindState.ToString(), d.Durability, d.ExpiresAtUtc
            }
        }).ToArray();
        return new { session = Session(preview.Session), preview.ObservedAt, preview.IsOpen, preview.AllItemsAvailable,
            initiatorItems = Items(preview.InitiatorItems), inviteeItems = Items(preview.InviteeItems) };
    }

    internal static void RejectDuplicateProperties(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            HashSet<string> keys = new(StringComparer.Ordinal);
            foreach (var property in element.EnumerateObject())
            {
                if (!keys.Add(property.Name)) throw new JsonException("Duplicate property.");
                RejectDuplicateProperties(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
            foreach (var child in element.EnumerateArray()) RejectDuplicateProperties(child);
    }
}
