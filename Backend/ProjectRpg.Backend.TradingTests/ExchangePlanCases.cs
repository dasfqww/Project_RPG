using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class ExchangePlanCases
{
    private static readonly DateTimeOffset Now = DateTimeOffset.UtcNow;
    private static readonly TradeParticipant A = new("76561198100000001", Guid.NewGuid()), B = new("76561198100000002", Guid.NewGuid());
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, false, 100)]);
    private static int checks;
    public static async Task<int> Run()
    {
        try
        {
            SwapAndCapacity(); SplitAndMerge(); Validation(); Wallets(); Conservation();
            await StoredReservations();
            Console.WriteLine($"PASS Exchange plan: 6 groups, {checks} assertions. Pure plan plus real Memory reservation compatibility; no atomic settlement/PG/HTTP/UE claim.");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
    private static ItemRecord Item(TradeParticipant p, int slot, int quantity = 1, int seed = 1) => new(
        "Material", "Ore", 1, new(ItemOwnerType.Character, p.CharacterId.ToString("D")),
        new(ItemContainerType.Inventory, p.CharacterId.ToString("D"), slot),
        new(Guid.NewGuid(), seed, quantity, ["Item.Ore"], [new("Stat.Value", 4)]), 1, ItemLifecycleState.Active,
        new(ItemBindState.Unbound, new(0, 0), null, "Test", false));
    private static TradeExchangeAssets Assets(TradeParticipant p, IReadOnlyList<ItemRecord> items, long balance = 1000,
        CurrencyScope scope = CurrencyScope.Character, long limit = 100000) {
        CharacterEconomyContext context = new(p.CharacterId, Guid.NewGuid(), p.AccountId);
        return new(context, items, new(new("Gold", "Gold", scope, limit, true),
            new(scope, EconomyRules.ResolveOwnerId(scope, context)), balance, 1), 0, new HashSet<Guid>());
    }
    private static TradeOffer Offer(ItemRecord item, int? quantity = null, long money = 0) => new([new(item.ItemId, item.Revision, quantity ?? item.State.Quantity)], money);
    private static TradeNegotiation Agreed(TradeOffer a, TradeOffer b, TradeParticipant? pa = null, TradeParticipant? pb = null)
    {
        pa ??= A; pb ??= B;
        var t = TradeNegotiation.Invite(Guid.NewGuid(), pa, pb, "Gold", Now, Now.AddHours(1));
        t = t.Accept(pb, t.Revision, Now);
        t = t.ReplaceOffer(pa, t.Revision, a, Now); t = t.ReplaceOffer(pb, t.Revision, b, Now);
        t = t.Ready(pa, t.Revision, t.OfferRevision, Now); t = t.Ready(pb, t.Revision, t.OfferRevision, Now);
        t = t.Confirm(pa, t.Revision, t.OfferRevision, Now); return t.Confirm(pb, t.Revision, t.OfferRevision, Now);
    }
    // Domain fixtures use the persisted reservation payload contract; StoredReservations also exercises the actual store.
    private static TradeAssetReservation Hold(TradeNegotiation t, TradeParticipant p, TradeOffer offer, CurrencyOwnerRef owner)
    {
        var c = new TradeReservationCommand(Guid.NewGuid(), t.TradeId, t.OfferRevision, p, t.CurrencyCode, offer, t.ExpiresAt);
        var payload = new { c.ReservationId, c.TradeId, c.OfferRevision, c.Participant, c.CurrencyCode,
            Items = c.Offer.Items.ToArray(), c.Offer.CurrencyAmount, c.ExpiresAt };
        return new(c, Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(payload)))), owner, TradeReservationStatus.Active);
    }
    private static TradeExchangePlan Plan(TradeNegotiation t, TradeExchangeAssets a, TradeExchangeAssets b) => TradeExchangePlanner.Plan(t, a, b,
        Hold(t, t.Initiator, t.InitiatorOffer, a.Currency.Owner), Hold(t, t.Invitee, t.InviteeOffer, b.Currency.Owner), Policy, Now);
    private static ItemRecord[] Apply(TradeExchangePlan plan, params TradeExchangeAssets[] assets)
    {
        var all = assets.SelectMany(a => a.Inventory).ToDictionary(i => i.ItemId);
        foreach (var m in plan.Items) { Check(m.NewRecord.Revision == m.ExpectedRevision, "CAS revision kept until commit");
            Check((all.TryGetValue(m.NewRecord.ItemId, out var old) ? old.Revision : 0) == m.ExpectedRevision, "CAS matches snapshot");
            Check(ItemRecordRules.TryValidateRecord(m.NewRecord, out _), "valid output record"); all[m.NewRecord.ItemId] = m.NewRecord; }
        var active = all.Values.Where(i => i.LifecycleState == ItemLifecycleState.Active).ToArray();
        Check(active.GroupBy(i => (i.Owner, i.Location)).All(g => g.Count() == 1), "unique final slots");
        Check(active.All(i => i.State.Quantity is > 0 and <= 100), "stack limit");
        Check(active.Sum(i => (long)i.State.Quantity) == assets.SelectMany(a => a.Inventory).Sum(i => (long)i.State.Quantity), "quantity conserved");
        return active;
    }
    private static void SwapAndCapacity()
    {
        var aa = Enumerable.Range(0, 80).Select(i => Item(A, i, seed: i + 1)).ToArray();
        var bb = Enumerable.Range(0, 80).Select(i => Item(B, i, seed: i + 100)).ToArray();
        var a = Assets(A, aa); var b = Assets(B, bb); var t = Agreed(Offer(aa[7]), Offer(bb[43]));
        var plan = Plan(t, a, b); var active = Apply(plan, a, b);
        Check(active.Single(i => i.ItemId == aa[7].ItemId).Location.SlotIndex == 43, "full bags reuse BOTH outgoing slots");
        var moved = active.Single(i => i.ItemId == bb[43].ItemId);
        Check(moved.State.GenerationSeed == bb[43].State.GenerationSeed && moved.State.StatValues.SequenceEqual(bb[43].State.StatValues)
            && moved.Metadata == bb[43].Metadata, "whole transfer preserves identity/seed/options");
        Reject(() => Plan(Agreed(Offer(aa[7]), TradeOffer.Empty), a, b), "trade_inventory_full");
        Check(a.Inventory[7] == aa[7] && b.Inventory[43] == bb[43], "failed planning has no side effects");
    }
    private static void SplitAndMerge()
    {
        var source = Item(A, 0, 60); var target = Item(B, 0, 90);
        var a = Assets(A, [source]); var b = Assets(B, [target]); var t = Agreed(Offer(source, 30), TradeOffer.Empty);
        var p = Plan(t, a, b); var active = Apply(p, a, b);
        Check(p.Splits.Single().SourceItemId == source.ItemId && p.Splits[0].Quantity == 30, "split origin retained");
        Check(active.Single(i => i.ItemId == source.ItemId).State.Quantity == 30, "source remainder");
        Check(active.Single(i => i.ItemId == target.ItemId).State.Quantity == 100, "merge to limit");
        Check(active.Single(i => i.ItemId == p.Splits[0].SplitItemId).State.Quantity == 20, "split residue takes free slot");
        Check(JsonSerializer.Serialize(p) == JsonSerializer.Serialize(Plan(t, a, b)), "retry produces identical plan/IDs");
        var merged = Plan(Agreed(Offer(source, 10), TradeOffer.Empty), a, b);
        Check(merged.Items.Single(m => m.NewRecord.ItemId == merged.Splits[0].SplitItemId).NewRecord.LifecycleState == ItemLifecycleState.Consumed, "fully merged split has tombstone");
        Apply(merged, a, b);
        var whole = Plan(Agreed(Offer(source), TradeOffer.Empty), a, Assets(B, [target with { State = target.State with { Quantity = 10 } }]));
        Check(whole.Items.Single(m => m.NewRecord.ItemId == source.ItemId).NewRecord.LifecycleState == ItemLifecycleState.Consumed, "fully merged original ID has tombstone");
        foreach (var blocked in new[] { target with { Metadata = target.Metadata with { IsLocked = true } },
            target with { Metadata = target.Metadata with { BindState = ItemBindState.AccountBound } },
            target with { State = target.State with { GenerationSeed = 2 } },
            target with { State = target.State with { StatValues = [new("Stat.Value", 5)] } },
            target with { DefinitionVersion = 2 }, target with { Revision = long.MaxValue } })
        {
            var q = Plan(t, a, Assets(B, [blocked])); Check(q.Items.All(m => m.NewRecord.ItemId != target.ItemId), "incompatible/protected stack not merged");
        }
        var reserved = b with { OtherReservedItems = new HashSet<Guid> { target.ItemId } };
        Check(Plan(t, a, reserved).Items.All(m => m.NewRecord.ItemId != target.ItemId), "other hold excluded from merge");
        var same = Agreed(Offer(source, 30), Offer(target, 10));
        Check(Plan(same, a, b).Items.Single(m => m.NewRecord.ItemId == target.ItemId).NewRecord.State.Quantity == 80, "outgoing remainder is not a merge target");
        var collision = Item(B, 2) with { State = target.State with { InstanceId = p.Splits[0].SplitItemId } };
        Reject(() => Plan(t, a, Assets(B, [target, collision])), "trade_split_id_conflict");
        try { ((IList<string>)p.Items[0].NewRecord.State.InstanceTags)[0] = "spoof"; throw new Exception("mutable result"); } catch (NotSupportedException) { checks++; }
        var small = new[] { Item(A, 0, 20), Item(A, 1, 30) };
        var almostFull = Enumerable.Range(0, 79).Select(i => Item(B, i, 100)).ToArray();
        var joined = Plan(Agreed(new(small.Select(i => new TradeOfferedItem(i.ItemId, i.Revision, i.State.Quantity)), 0), TradeOffer.Empty),
            Assets(A, small), Assets(B, almostFull));
        var joinedItems = Apply(joined, Assets(A, small), Assets(B, almostFull));
        Check(joinedItems.Count(i => i.Owner.OwnerId == B.CharacterId.ToString("D")) == 80
            && joinedItems.Any(i => i.State.Quantity == 50), "incoming whole stacks merge with each other into last free slot");
    }
    private static void Validation()
    {
        var item = Item(A, 0, 20); var a = Assets(A, [item]); var b = Assets(B, []); var t = Agreed(Offer(item), TradeOffer.Empty);
        var ra = Hold(t, A, t.InitiatorOffer, a.Currency.Owner); var rb = Hold(t, B, t.InviteeOffer, b.Currency.Owner);
        foreach (var bad in new[] { ra with { Status = TradeReservationStatus.Released }, ra with { Fingerprint = "bad" },
            ra with { Command = ra.Command with { OfferRevision = 1 } }, ra with { Command = ra.Command with { ExpiresAt = Now } },
            ra with { Command = ra.Command with { Offer = new([], 1) } }, ra with { WalletOwner = rb.WalletOwner } })
            Reject(() => TradeExchangePlanner.Plan(t, a, b, bad, rb, Policy, Now), "invalid_trade_reservation");
        Reject(() => TradeExchangePlanner.Plan(t, a, b, ra, rb, Policy, t.ExpiresAt), "trade_expired");
        Reject(() => Plan(t.Cancel(A, t.Revision, Now), a, b), "trade_not_agreed");
        Reject(() => Plan(t, a with { OtherReservedItems = new HashSet<Guid> { item.ItemId } }, b), "reservation_item_busy");
        Reject(() => Plan(t, a with { Inventory = [item with { Revision = 2 }] }, b), "item_revision_conflict");
        Reject(() => Plan(t, a with { Inventory = [item with { Metadata = item.Metadata with { ExpiresAtUtc = Now } }] }, b), "item_unavailable");
        Reject(() => Plan(t, a with { Inventory = [item with { Metadata = item.Metadata with { BindState = ItemBindState.CharacterBound } }] }, b), "item_bound");
        Reject(() => Plan(t, a with { Inventory = [] }, b), "trade_item_not_found");
        Reject(() => Plan(t, a with { Inventory = [item, Item(A, 0)] }, b), "invalid_trade_inventory");
        Reject(() => Plan(t, a with { Inventory = [item with { Location = item.Location with { SlotIndex = 80 } }] }, b), "invalid_trade_inventory");
        Reject(() => Plan(t, a with { Context = a.Context with { SteamId = B.AccountId } }, b), "invalid_trade_inventory");
    }
    private static void Wallets()
    {
        foreach (CurrencyScope scope in Enum.GetValues<CurrencyScope>()) {
            var a = Assets(A, [], 1000, scope); var b = Assets(B, [], 2000, scope);
            var p = Plan(Agreed(new([], 600), new([], 200)), a, b);
            Check(p.Currency.Sum(c => c.PreviousBalance) == p.Currency.Sum(c => c.NewBalance), "money conserved in every wallet scope");
            Check(p.Currency[0].NewBalance == 600 && p.Currency[1].NewBalance == 2400, "bilateral money");
            Reject(() => Plan(Agreed(new([], 600), new([], 200)), a with { OtherReservedCurrency = 401 }, b), "trade_insufficient_balance");
            Check(Plan(Agreed(new([], 600), new([], 600)), a, b).Currency.Count == 0, "equal gross offers require no writes");
            Reject(() => Plan(Agreed(new([], 1001), new([], 1001)), a, b), "trade_insufficient_balance");
        }
        var rich = Assets(A, [], long.MaxValue, limit: long.MaxValue); var poor = Assets(B, [], 1, limit: long.MaxValue);
        Reject(() => Plan(Agreed(new([], 0), new([], 1)), rich, poor), "trade_balance_limit");
        var a2 = Assets(A, [], 50, limit: 100); var b2 = Assets(B, [], 100, limit: 100);
        Reject(() => Plan(Agreed(new([], 1), TradeOffer.Empty), a2, b2), "trade_balance_limit");
        Reject(() => Plan(Agreed(new([], 1), TradeOffer.Empty), a2 with { Currency = a2.Currency with { Revision = long.MaxValue } }, b2 with { Currency = b2.Currency with { Balance = 0 } }), "trade_revision_exhausted");
        Reject(() => Plan(Agreed(new([], 1), TradeOffer.Empty), a2 with { Currency = a2.Currency with { Owner = b2.Currency.Owner } }, b2), "invalid_trade_wallet");
    }
    private static void Conservation()
    {
        var random = new Random(314159);
        for (int n = 0; n < 80; n++) {
            var aa = Enumerable.Range(0, 10).Select(i => Item(A, i, random.Next(2, 100))).ToArray();
            var bb = Enumerable.Range(0, 10).Select(i => Item(B, i, random.Next(2, 100))).ToArray();
            var a = Assets(A, aa); var b = Assets(B, bb);
            var oa = new TradeOffer(aa.Take(8).Select(i => new TradeOfferedItem(i.ItemId, i.Revision, random.Next(1, i.State.Quantity + 1))), n);
            var ob = new TradeOffer(bb.Take(8).Select(i => new TradeOfferedItem(i.ItemId, i.Revision, random.Next(1, i.State.Quantity + 1))), 79 - n);
            var t = Agreed(oa, ob); var p = Plan(t, a, b); Apply(p, a, b);
            Check(p.Items.Count <= TradeExchangePlanner.MaximumMutationCount && p.Items.Select(m => m.NewRecord.ItemId).Distinct().Count() == p.Items.Count, "bounded unique mutations");
            Check(JsonSerializer.Serialize(p) == JsonSerializer.Serialize(Plan(t, a with { Inventory = aa.Reverse().ToArray() }, b with { Inventory = bb.Reverse().ToArray() })), "input order independent");
        }
    }
    private static async Task StoredReservations()
    {
        var gate = new InMemoryTransactionGate(); var items = new InMemoryItemRepository(gate); var economy = new InMemoryEconomyRepository(gate);
        var game = new InMemoryGameRepository(); var store = new InMemoryTradeReservationRepository(gate, items, economy, game, Policy);
        var assets = new List<TradeExchangeAssets>(); var people = new List<TradeParticipant>();
        await economy.UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Account, 10000, true), Now, default);
        foreach (var id in new[] { A.AccountId, B.AccountId }) {
            await game.UpsertAccountAsync(new(id, id, false, false), Now, default);
            var character = await game.CreateCharacterAsync(id, "TradeTest" + people.Count, Now, default);
            var context = (await game.GetCharacterEconomyContextAsync(character.CharacterId, default))!;
            var funded = await economy.CommitAsync(new(Guid.NewGuid(), character.CharacterId, Guid.NewGuid(), "Test.Fund", "fund", "test", [new("Gold", 100)]), context, Now, default);
            Check(funded.Status == CurrencyTransactionStatus.Committed, "memory fixture funded");
            assets.Add(new(context, [], (await economy.LoadWalletAsync(context, default)).Balances.Single(), 0, new HashSet<Guid>())); people.Add(new(id, character.CharacterId));
        }
        var t = Agreed(new([], 30), TradeOffer.Empty, people[0], people[1]);
        var first = await store.ReserveAsync(new(Guid.NewGuid(), t.TradeId, t.OfferRevision, people[0], "Gold", t.InitiatorOffer, t.ExpiresAt), default);
        var second = await store.ReserveAsync(new(Guid.NewGuid(), t.TradeId, t.OfferRevision, people[1], "Gold", t.InviteeOffer, t.ExpiresAt), default);
        var p = TradeExchangePlanner.Plan(t, assets[0], assets[1], first, second, Policy, Now);
        Check(p.Currency[0].NewBalance == 70 && p.Currency[1].NewBalance == 130, "actual reservation fingerprint/owner accepted");
        Check((await economy.LoadWalletAsync(assets[0].Context, default)).Balances.Single().Balance == 100, "planning does not commit money");
        Check((await store.FindAsync(first.Command.ReservationId, people[0], default))!.Status == TradeReservationStatus.Active, "planning does not release hold");
    }
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); checks++; }
    private static void Reject(Action action, string code) { try { action(); } catch (TradeRuleException e) when (e.Code == code) { checks++; return; } throw new Exception("Expected " + code); }
}
