using System.Collections.ObjectModel;
using System.Security.Cryptography;
using System.Text;

namespace ProjectRpg.Backend.Domain.Trading;

// Loaded by the server under transaction locks. Other holds exclude THIS trade's reservation.
public sealed record TradeExchangeAssets(CharacterEconomyContext Context, IReadOnlyList<ItemRecord> Inventory,
    CurrencyBalance Currency, long OtherReservedCurrency, IReadOnlySet<Guid> OtherReservedItems);
public sealed record TradeSplitOrigin(Guid SourceItemId, Guid SplitItemId, int Quantity);
public sealed record TradeCurrencyMovement(CurrencyOwnerRef Owner, string CurrencyCode, long ExpectedRevision,
    long PreviousBalance, long NewBalance);
public sealed record TradeExchangePlan(Guid TradeId, long SessionRevision, long OfferRevision,
    ReadOnlyCollection<ItemRecordMutation> Items, ReadOnlyCollection<TradeCurrencyMovement> Currency,
    ReadOnlyCollection<TradeSplitOrigin> Splits);

/// <summary>
/// Pure bilateral plan, never an authorization or a committed receipt. The store must lock/revalidate both
/// participants, persist final consent, all mutations, reservation release and receipt in ONE transaction.
/// Inventory snapshots must be complete, and newly derived IDs must be checked globally by that store.
/// </summary>
public static class TradeExchangePlanner
{
    public const int InventoryCapacity = DungeonRewardDeliveryPlanner.InventorySlotCapacity;
    // Both entire bags can be merge targets, plus one new identity for each partial outgoing stack.
    // Do not submit this plan through the public 16-mutation item API or a forged dungeon reward command.
    public const int MaximumMutationCount = 2 * (InventoryCapacity + TradeOffer.MaximumItemCount);

    public static TradeExchangePlan Plan(TradeNegotiation trade, TradeExchangeAssets first, TradeExchangeAssets second,
        TradeAssetReservation firstReservation, TradeAssetReservation secondReservation,
        TradeItemPolicy policy, DateTimeOffset now)
    {
        if (trade.Status != TradeNegotiationStatus.Agreed || !trade.InitiatorReady || !trade.InviteeReady
            || !trade.InitiatorConfirmed || !trade.InviteeConfirmed)
            throw new TradeRuleException("trade_not_agreed");
        if (trade.ExpiresAt <= now) throw new TradeRuleException("trade_expired");
        var a = Inventory(first, trade.Initiator);
        var b = Inventory(second, trade.Invitee);
        if (a.Keys.Intersect(b.Keys).Any()) throw new TradeRuleException("invalid_trade_inventory");
        Reservation(trade, firstReservation, trade.Initiator, trade.InitiatorOffer, first.Currency.Owner, now);
        Reservation(trade, secondReservation, trade.Invitee, trade.InviteeOffer, second.Currency.Owner, now);
        if (firstReservation.Command.ReservationId == secondReservation.Command.ReservationId)
            throw new TradeRuleException("invalid_trade_reservation");
        var money = new[]
        {
            Currency(trade, first, trade.Initiator, trade.InitiatorOffer.CurrencyAmount, trade.InviteeOffer.CurrencyAmount),
            Currency(trade, second, trade.Invitee, trade.InviteeOffer.CurrencyAmount, trade.InitiatorOffer.CurrencyAmount)
        };
        if (first.Currency.Definition != second.Currency.Definition || money[0].Owner == money[1].Owner)
            throw new TradeRuleException("invalid_trade_wallet");

        Dictionary<Guid, ItemRecordMutation> mutations = [];
        List<TradeSplitOrigin> splits = [];
        HashSet<Guid> known = [.. a.Keys, .. b.Keys];
        // Withdraw BOTH sides before planning arrivals: a full-bag swap must reuse outgoing slots.
        var fromA = Withdraw(trade, a, first, trade.Initiator, trade.InitiatorOffer, policy, now, known, mutations, splits);
        var fromB = Withdraw(trade, b, second, trade.Invitee, trade.InviteeOffer, policy, now, known, mutations, splits);
        Deliver(fromA, b, second, trade.InviteeOffer.Items.Select(i => i.ItemId).ToHashSet(), policy, now, mutations);
        Deliver(fromB, a, first, trade.InitiatorOffer.Items.Select(i => i.ItemId).ToHashSet(), policy, now, mutations);
        if (mutations.Count > MaximumMutationCount) throw new TradeRuleException("trade_mutation_limit");
        foreach (var mutation in mutations.Values)
            if (!ItemRecordRules.TryValidateRecord(mutation.NewRecord, out _) || mutation.ExpectedRevision == long.MaxValue)
                throw new TradeRuleException("invalid_trade_mutation");
        return new(trade.TradeId, trade.Revision, trade.OfferRevision,
            Array.AsReadOnly(mutations.Values.OrderBy(m => m.NewRecord.ItemId).ToArray()),
            Array.AsReadOnly(money.Where(m => m.PreviousBalance != m.NewBalance).ToArray()),
            Array.AsReadOnly(splits.ToArray()));
    }

    private static Dictionary<Guid, ItemRecord> Inventory(TradeExchangeAssets assets, TradeParticipant participant)
    {
        if (assets.Context.CharacterId != participant.CharacterId || assets.Context.SteamId != participant.AccountId
            || assets.Context.RosterId == Guid.Empty || assets.Inventory.Count > InventoryCapacity)
            throw new TradeRuleException("invalid_trade_inventory");
        Dictionary<Guid, ItemRecord> inventory = [];
        HashSet<int> slots = [];
        ItemOwnerRef owner = new(ItemOwnerType.Character, participant.CharacterId.ToString("D"));
        foreach (var item in assets.Inventory)
        {
            if (!ItemRecordRules.TryValidateRecord(item, out _) || item.Revision <= 0
                || !DungeonRewardDeliveryPlanner.IsInventoryRecord(item, owner)
                || !slots.Add(item.Location.SlotIndex) || !inventory.TryAdd(item.ItemId, Freeze(item)))
                throw new TradeRuleException("invalid_trade_inventory");
        }
        return inventory;
    }

    private static void Reservation(TradeNegotiation trade, TradeAssetReservation reservation,
        TradeParticipant participant, TradeOffer offer, CurrencyOwnerRef wallet, DateTimeOffset now)
    {
        var command = TradeReservationRules.Validate(reservation.Command);
        if (reservation.At(now).Status != TradeReservationStatus.Active || command.TradeId != trade.TradeId
            || command.Participant != participant || command.OfferRevision != trade.OfferRevision
            || command.CurrencyCode != trade.CurrencyCode || command.ExpiresAt > trade.ExpiresAt
            || reservation.WalletOwner != wallet || command.Offer.CurrencyAmount != offer.CurrencyAmount
            || !command.Offer.Items.SequenceEqual(offer.Items)
            || reservation.Fingerprint != TradeReservationRules.Fingerprint(command))
            throw new TradeRuleException("invalid_trade_reservation");
    }

    private static TradeCurrencyMovement Currency(TradeNegotiation trade, TradeExchangeAssets assets,
        TradeParticipant participant, long outgoing, long incoming)
    {
        var balance = assets.Currency;
        if (!EconomyRules.TryValidateDefinition(balance.Definition, out _) || !balance.Definition.Enabled
            || !Enum.IsDefined(balance.Definition.Scope) || balance.Definition.CurrencyCode != trade.CurrencyCode
            || balance.Owner != new CurrencyOwnerRef(balance.Definition.Scope,
                EconomyRules.ResolveOwnerId(balance.Definition.Scope, assets.Context))
            || assets.Context.CharacterId != participant.CharacterId || balance.Balance < 0
            || balance.Balance > balance.Definition.MaxBalance || balance.Revision < 0
            || assets.OtherReservedCurrency < 0 || assets.OtherReservedCurrency > balance.Balance)
            throw new TradeRuleException("invalid_trade_wallet");
        // Require the gross offer to be funded, even if the net change is zero.
        if (outgoing > balance.Balance - assets.OtherReservedCurrency)
            throw new TradeRuleException("trade_insufficient_balance");
        long next;
        try { next = checked(balance.Balance - outgoing + incoming); }
        catch (OverflowException) { throw new TradeRuleException("trade_balance_limit"); }
        if (next > balance.Definition.MaxBalance) throw new TradeRuleException("trade_balance_limit");
        if (next != balance.Balance && balance.Revision == long.MaxValue)
            throw new TradeRuleException("trade_revision_exhausted");
        return new(balance.Owner, trade.CurrencyCode, balance.Revision, balance.Balance, next);
    }

    private static List<ItemRecord> Withdraw(TradeNegotiation trade, Dictionary<Guid, ItemRecord> inventory,
        TradeExchangeAssets assets, TradeParticipant seller, TradeOffer offer, TradeItemPolicy policy, DateTimeOffset now,
        HashSet<Guid> known, Dictionary<Guid, ItemRecordMutation> mutations, List<TradeSplitOrigin> splits)
    {
        List<ItemRecord> outgoing = [];
        foreach (var offered in offer.Items)
        {
            if (!inventory.TryGetValue(offered.ItemId, out var source)) throw new TradeRuleException("trade_item_not_found");
            policy.ValidateOfferItem(source, seller, offered, TradeChannel.Direct, now);
            if (assets.OtherReservedItems.Contains(source.ItemId)) throw new TradeRuleException("reservation_item_busy");
            if (offered.Quantity == source.State.Quantity)
            {
                inventory.Remove(source.ItemId);
                outgoing.Add(source);
            }
            else
            {
                var remainder = source with { State = source.State with { Quantity = source.State.Quantity - offered.Quantity } };
                inventory[source.ItemId] = remainder;
                mutations[source.ItemId] = new(source.Revision, remainder);
                Guid id = SplitId(trade.TradeId, trade.OfferRevision, source.ItemId);
                if (!known.Add(id)) throw new TradeRuleException("trade_split_id_conflict");
                outgoing.Add(source with { Revision = 0, State = source.State with { InstanceId = id, Quantity = offered.Quantity } });
                splits.Add(new(source.ItemId, id, offered.Quantity));
            }
        }
        return outgoing;
    }

    private static void Deliver(List<ItemRecord> incoming, Dictionary<Guid, ItemRecord> inventory,
        TradeExchangeAssets recipient, HashSet<Guid> offered, TradeItemPolicy policy, DateTimeOffset now,
        Dictionary<Guid, ItemRecordMutation> mutations)
    {
        ItemOwnerRef owner = new(ItemOwnerType.Character, recipient.Context.CharacterId.ToString("D"));
        foreach (var item in incoming)
        {
            int remaining = item.State.Quantity;
            int maximum = policy.MaximumStackSize(item);
            foreach (var target in inventory.Values.OrderBy(i => i.Location.SlotIndex).ThenBy(i => i.ItemId).ToArray())
            {
                if (remaining == 0) break;
                if (offered.Contains(target.ItemId) || recipient.OtherReservedItems.Contains(target.ItemId)
                    || target.Revision == long.MaxValue || !CanMerge(target, item, now) || target.State.Quantity >= maximum) continue;
                int amount = Math.Min(remaining, maximum - target.State.Quantity);
                var merged = target with { State = target.State with { Quantity = target.State.Quantity + amount } };
                inventory[target.ItemId] = merged;
                mutations[target.ItemId] = new(target.Revision, merged);
                remaining -= amount;
            }
            ItemRecord moved;
            if (remaining == 0)
            {
                moved = item with { Owner = owner, State = item.State with { Quantity = 0 },
                    LifecycleState = ItemLifecycleState.Consumed, Location = new(ItemContainerType.Terminal, "", -1) };
            }
            else
            {
                var occupied = inventory.Values.Select(i => i.Location.SlotIndex).ToHashSet();
                int slot = Enumerable.Range(0, InventoryCapacity).FirstOrDefault(i => !occupied.Contains(i), -1);
                if (slot < 0) throw new TradeRuleException("trade_inventory_full");
                moved = item with { Owner = owner, State = item.State with { Quantity = remaining },
                    Location = new(ItemContainerType.Inventory, owner.OwnerId, slot) };
                inventory.Add(moved.ItemId, moved);
            }
            mutations[item.ItemId] = new(item.Revision, moved);
        }
    }

    private static bool CanMerge(ItemRecord a, ItemRecord b, DateTimeOffset now) =>
        a.State.EquipmentProgression is null && b.State.EquipmentProgression is null
        && a.DefinitionType == b.DefinitionType && a.DefinitionName == b.DefinitionName && a.DefinitionVersion == b.DefinitionVersion
        && !a.Metadata.IsLocked && a.Metadata.BindState == b.Metadata.BindState && a.Metadata.Durability == b.Metadata.Durability
        && a.Metadata.ExpiresAtUtc == b.Metadata.ExpiresAtUtc && (a.Metadata.ExpiresAtUtc is null || a.Metadata.ExpiresAtUtc > now)
        && a.State.GenerationSeed == b.State.GenerationSeed
        && a.State.InstanceTags.Order(StringComparer.Ordinal).SequenceEqual(b.State.InstanceTags.Order(StringComparer.Ordinal))
        && a.State.StatValues.OrderBy(s => s.StatTag, StringComparer.Ordinal).SequenceEqual(b.State.StatValues.OrderBy(s => s.StatTag, StringComparer.Ordinal));

    private static ItemRecord Freeze(ItemRecord item) => item with { State = item.State with
        { InstanceTags = Array.AsReadOnly(item.State.InstanceTags.ToArray()), StatValues = Array.AsReadOnly(item.State.StatValues.ToArray()) } };

    private static Guid SplitId(Guid trade, long offerRevision, Guid source) => new(SHA256.HashData(
        Encoding.UTF8.GetBytes(FormattableString.Invariant($"trade_split|{trade:D}|{offerRevision}|{source:D}"))).AsSpan(0, 16));
}
