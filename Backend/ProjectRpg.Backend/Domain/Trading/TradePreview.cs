namespace ProjectRpg.Backend.Domain.Trading;

// Public gameplay details only. Never embed ItemRecord (owner/location/source/seed) in a preview.
public sealed record TradeItemDetails(string DefinitionType, string DefinitionName, int DefinitionVersion,
    IReadOnlyList<string> InstanceTags, IReadOnlyList<ItemStatValue> StatValues,
    FirstSliceEquipmentProgressionState? EquipmentProgression, ItemBindState BindState,
    ItemDurability Durability, DateTimeOffset? ExpiresAtUtc);
public sealed record TradePreviewItem(Guid ItemId, long ExpectedRevision, int Quantity, TradeItemDetails? Details);
public sealed record TradePreview(TradeStoredSession Session, DateTimeOffset ObservedAt, bool IsOpen,
    IReadOnlyList<TradePreviewItem> InitiatorItems, IReadOnlyList<TradePreviewItem> InviteeItems)
{
    // Item checks only: not evidence of currency, capacity, admission, reservations or consent.
    public bool AllItemsAvailable => IsOpen && InitiatorItems.Concat(InviteeItems).All(i => i.Details is not null);

    internal static bool CanInspect(TradeStoredSession s, DateTimeOffset now) => !s.Completed
        && s.State.Status is not (TradeNegotiationStatus.Cancelled or TradeNegotiationStatus.Expired) && s.State.ExpiresAt > now;

    internal static TradePreview Create(TradeStoredSession session, IReadOnlyDictionary<Guid, ItemRecord> records,
        TradeItemPolicy policy, DateTimeOffset now)
    {
        bool open = CanInspect(session, now);
        IReadOnlyList<TradePreviewItem> Side(TradeParticipant seller, TradeOffer offer) => Array.AsReadOnly(offer.Items.Select(offered =>
        {
            TradeItemDetails? details = null;
            if (open && records.TryGetValue(offered.ItemId, out var item))
            {
                try
                {
                    // Do not leak foreign, stale, locked, bound, disallowed or expired records referenced by a forged offer.
                    policy.ValidateOfferItem(item, seller, offered, TradeChannel.Direct, now);
                    details = new(item.DefinitionType, item.DefinitionName, item.DefinitionVersion,
                        Array.AsReadOnly(item.State.InstanceTags.ToArray()), Array.AsReadOnly(item.State.StatValues.ToArray()),
                        item.State.EquipmentProgression, item.Metadata.BindState, item.Metadata.Durability, item.Metadata.ExpiresAtUtc);
                }
                catch (TradeRuleException) { /* All unavailable cases have the same public projection. */ }
            }
            return new TradePreviewItem(offered.ItemId, offered.ExpectedRevision, offered.Quantity, details);
        }).ToArray());
        return new(session, now, open, Side(session.State.Initiator, session.State.InitiatorOffer), Side(session.State.Invitee, session.State.InviteeOffer));
    }
}
