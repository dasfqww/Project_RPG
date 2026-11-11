using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

if (args is ["--auction-domain"]) return AuctionCases.Run();
if (args is ["--auction-memory"]) return await AuctionMemoryCases.Run();
if (args is ["--auction-wire-fixtures", var fixtureDirectory]) return await AuctionWireFixtures.Run(fixtureDirectory);
if (args is ["--auction-api-memory"] or ["--auction-api-postgres"] or ["--auction-api-restart"]) return await AuctionApiCases.Run(args[0]);
if (args is ["--auction-claim"] or ["--auction-claim-restart"]) return await AuctionClaimCases.Run(args[0] == "--auction-claim-restart");
if (args is ["--auction-purchase"] or ["--auction-purchase-restart"]) return await AuctionPurchaseCases.Run(args[0] == "--auction-purchase-restart");
if (args is ["--auction-escrow"] or ["--auction-restart"]) return await AuctionEscrowCases.Run(args[0] == "--auction-restart");
if (args is ["--api-memory"] or ["--api-postgres"] or ["--api-restart"]) return await ApiCases.Run(args[0]);
if (args is ["--sessions-memory"]) return await MemorySessionCases.Run();
if (args is ["--sessions-postgres"] or ["--sessions-restart"]) return await SessionCases.Run(args[0] == "--sessions-restart");
if (args is ["--exchange-plan"]) return await ExchangePlanCases.Run();
if (args is ["--reservations-memory"] or ["--reservations-postgres"])
    return await ReservationCases.Run(args[0] == "--reservations-postgres");
if (args.Length != 0) throw new ArgumentException("Unknown trading test mode.");
return TradingCases.Run();

internal static class TradingCases
{
    private static readonly DateTimeOffset Now = new(2026, 11, 6, 0, 0, 0, TimeSpan.Zero);
    private static readonly TradeParticipant Alice = new("account-alice", Guid.Parse("11111111-1111-4111-8111-111111111111"));
    private static readonly TradeParticipant Bob = new("account-bob", Guid.Parse("22222222-2222-4222-8222-222222222222"));
    private static readonly TradeParticipant Eve = new("account-eve", Guid.Parse("33333333-3333-4333-8333-333333333333"));
    private static int assertions;

    public static int Run()
    {
        try
        {
            OfferSnapshots();
            InvitationAccess();
            ConsentAndEdits();
            Termination();
            DefinitionAndOwnership();
            ItemBoundaries();
            Console.WriteLine($"PASS Trading: 6 groups, {assertions} assertions. Domain only; no DB/HTTP/UE claim.");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"FAIL Trading: {ex.Message}\n{ex.StackTrace}");
            return 1;
        }
    }

    private static TradeNegotiation Invite() => TradeNegotiation.Invite(Guid.NewGuid(), Alice, Bob, "TestGold", Now, Now.AddMinutes(5));
    private static TradeNegotiation Open()
    {
        TradeNegotiation state = Invite();
        return state.Accept(Bob, state.Revision, Now);
    }
    private static TradeOfferedItem Offered(int quantity = 1) => new(Guid.NewGuid(), 7, quantity);
    private static TradeNegotiation ReadyBoth()
    {
        TradeNegotiation state = Open();
        state = state.ReplaceOffer(Alice, state.Revision, new([Offered()], 0), Now);
        state = state.ReplaceOffer(Bob, state.Revision, new([], 700), Now);
        state = state.Ready(Alice, state.Revision, state.OfferRevision, Now);
        return state.Ready(Bob, state.Revision, state.OfferRevision, Now);
    }

    private static void OfferSnapshots()
    {
        TradeOfferedItem original = Offered(30);
        TradeOfferedItem[] source = [original];
        TradeOffer offer = new(source, long.MaxValue);
        source[0] = Offered();
        Check(offer.Items.Single() == original && offer.CurrencyAmount == long.MaxValue, "Offer owns its input snapshot and exact int64 amount.");
        try { ((IList<TradeOfferedItem>)offer.Items)[0] = Offered(); throw new Exception("Offer collection was writable."); }
        catch (NotSupportedException) { assertions++; }
        Reject(() => new TradeOffer([original, original], 0), "invalid_offer");
        Reject(() => new TradeOffer(Enumerable.Range(0, 9).Select(_ => Offered()), 0), "invalid_offer");
        Reject(() => new TradeOffer([], -1), "invalid_offer");
        foreach (TradeOfferedItem bad in new[] { original with { ItemId = Guid.Empty }, original with { Quantity = 0 },
                     original with { Quantity = -1 }, original with { ExpectedRevision = 0 },
                     original with { ExpectedRevision = long.MaxValue }, original with { ExpectedRevision = -1 } })
            Reject(() => new TradeOffer([bad], 0), "invalid_offer");
        Check(TradeOffer.Empty.IsEmpty && !new TradeOffer([], 1).IsEmpty, "Currency-only offers supported.");
        Console.WriteLine("PASS offer snapshots, limits and exact integer amounts");
    }

    private static void InvitationAccess()
    {
        Reject(() => TradeNegotiation.Invite(Guid.Empty, Alice, Bob, "Gold", Now, Now.AddMinutes(1)), "invalid_trade_invitation");
        Reject(() => TradeNegotiation.Invite(Guid.NewGuid(), Alice, Bob with { AccountId = Alice.AccountId }, "Gold", Now, Now.AddMinutes(1)), "invalid_trade_invitation");
        Reject(() => TradeNegotiation.Invite(Guid.NewGuid(), Alice, Bob with { CharacterId = Alice.CharacterId }, "Gold", Now, Now.AddMinutes(1)), "invalid_trade_invitation");
        Reject(() => TradeNegotiation.Invite(Guid.NewGuid(), Alice, Bob, "Gold", Now, Now), "invalid_trade_invitation");
        Reject(() => TradeNegotiation.Invite(Guid.NewGuid(), Alice, Bob, "Gold\n", Now, Now.AddMinutes(1)), "invalid_trade_invitation");
        TradeNegotiation state = Invite();
        Reject(() => state.Accept(Alice, state.Revision, Now), "trade_invitation_not_available");
        Reject(() => state.Accept(Eve, state.Revision, Now), "trade_participant_required");
        Reject(() => state.Accept(Bob with { AccountId = Eve.AccountId }, state.Revision, Now), "trade_participant_required");
        Reject(() => state.ReplaceOffer(Alice, state.Revision, new([], 1), Now), "trade_not_negotiating");
        TradeNegotiation accepted = state.Accept(Bob, state.Revision, Now);
        Check(state.Status == TradeNegotiationStatus.Invited && accepted.Status == TradeNegotiationStatus.Negotiating,
            "Transition preserves old snapshot.");
        Reject(() => accepted.Ready(Alice, accepted.Revision, accepted.OfferRevision, Now), "empty_trade");
        Reject(() => accepted.Accept(Bob, accepted.Revision, Now), "trade_invitation_not_available");
        Console.WriteLine("PASS invitation ownership and forged participant rejection");
    }

    private static void ConsentAndEdits()
    {
        // Exercise both orderings: a recipient giving nothing must still explicitly consent.
        foreach (bool aliceFirst in new[] { true, false })
        {
            TradeNegotiation state = Open();
            state = state.ReplaceOffer(Alice, state.Revision, new([], 1000), Now);
            Reject(() => state.Confirm(Alice, state.Revision, state.OfferRevision, Now), "trade_not_ready");
            state = state.Ready(Alice, state.Revision, state.OfferRevision, Now);
            state = state.Ready(Bob, state.Revision, state.OfferRevision, Now);
            TradeParticipant first = aliceFirst ? Alice : Bob, second = aliceFirst ? Bob : Alice;
            TradeNegotiation one = state.Confirm(first, state.Revision, state.OfferRevision, Now);
            Check(one.Status == TradeNegotiationStatus.Negotiating, "One confirmation cannot settle.");
            Check(ReferenceEquals(one, one.Confirm(first, one.Revision, one.OfferRevision, Now)), "Repeated confirmation has no transition.");
            Reject(() => one.Confirm(second, state.Revision, one.OfferRevision, Now), "trade_revision_conflict");
            TradeNegotiation agreed = one.Confirm(second, one.Revision, one.OfferRevision, Now);
            Check(agreed.Status == TradeNegotiationStatus.Agreed && agreed.InitiatorConfirmed && agreed.InviteeConfirmed,
                "Both current confirmations produce agreement, not a payment receipt.");
            Reject(() => agreed.ReplaceOffer(Alice, agreed.Revision, new([], 2), Now), "trade_not_negotiating");
        }
        TradeNegotiation ready = ReadyBoth();
        ready = ready.Confirm(Bob, ready.Revision, ready.OfferRevision, Now);
        foreach (TradeParticipant actor in new[] { Alice, Bob })
        {
            foreach (TradeOffer replacement in new[] { new TradeOffer([], 0), new TradeOffer([], 900), new TradeOffer([Offered(2)], 0) })
            {
                TradeNegotiation edited = ready.ReplaceOffer(actor, ready.Revision, replacement, Now);
                Check(!edited.InitiatorReady && !edited.InviteeReady && !edited.InitiatorConfirmed && !edited.InviteeConfirmed
                    && edited.OfferRevision == ready.OfferRevision + 1, "Any side's edit clears all consent.");
                Reject(() => edited.Ready(Alice, edited.Revision, ready.OfferRevision, Now), "trade_offer_revision_conflict");
                Reject(() => edited.Confirm(Bob, ready.Revision, ready.OfferRevision, Now), "trade_revision_conflict");
            }
        }
        Reject(() => ready.ReplaceOffer(Bob, ready.Revision, new(ready.InitiatorOffer.Items, 0), Now), "duplicate_trade_item");
        Reject(() => ready.Ready(Eve, ready.Revision, ready.OfferRevision, Now), "trade_participant_required");
        Reject(() => ready.Confirm(Eve, ready.Revision, ready.OfferRevision, Now), "trade_participant_required");
        Reject(() => ready.ReplaceOffer(Eve, ready.Revision, TradeOffer.Empty, Now), "trade_participant_required");
        TradeNegotiation withdrawn = ready.WithdrawReady(Alice, ready.Revision, Now);
        Check(!withdrawn.InitiatorReady && !withdrawn.InviteeReady && !withdrawn.InviteeConfirmed
            && withdrawn.OfferRevision == ready.OfferRevision, "Unready clears consent without changing content version.");
        Reject(() => withdrawn.Confirm(Bob, ready.Revision, ready.OfferRevision, Now), "trade_revision_conflict");
        Console.WriteLine("PASS bilateral consent, gifts, edit substitution and stale request scenarios");
    }

    private static void Termination()
    {
        TradeNegotiation state = ReadyBoth();
        string before = JsonSerializer.Serialize(state);
        Reject(() => state.Cancel(Eve, state.Revision, Now), "trade_participant_required");
        Reject(() => state.Expire(state.Revision, state.ExpiresAt.AddTicks(-1)), "trade_not_expired");
        Reject(() => state.Confirm(Alice, state.Revision, state.OfferRevision, state.ExpiresAt), "trade_expired");
        TradeNegotiation expired = state.Expire(state.Revision, state.ExpiresAt);
        Check(expired.Status == TradeNegotiationStatus.Expired && !expired.InitiatorReady && !expired.InviteeReady, "Expiry clears readiness at exact deadline.");
        TradeNegotiation cancelled = state.Cancel(Bob, state.Revision, Now);
        Check(cancelled.Status == TradeNegotiationStatus.Cancelled && !cancelled.InitiatorReady, "Either participant can cancel.");
        Reject(() => cancelled.Ready(Alice, cancelled.Revision, cancelled.OfferRevision, Now), "trade_closed");
        Reject(() => expired.Cancel(Alice, expired.Revision, state.ExpiresAt), "trade_closed");
        Check(state.Cancel(Alice, state.Revision, state.ExpiresAt).Status == TradeNegotiationStatus.Expired, "Late cancel records expiry.");
        Check(JsonSerializer.Serialize(state) == before, "Failures and derived states do not mutate source.");
        Check(Invite().Cancel(Bob, 1, Now).Status == TradeNegotiationStatus.Cancelled, "Invitee may decline.");
        Console.WriteLine("PASS cancellation, expiry boundary and immutable failure behavior");
    }

    private static ItemRecord Item() => new("Material", "Test.Ore", 1,
        new(ItemOwnerType.Character, Alice.CharacterId.ToString("D")),
        new(ItemContainerType.Inventory, Alice.CharacterId.ToString("D"), 0),
        new(Guid.NewGuid(), 3, 100, [], []), 7, ItemLifecycleState.Active,
        new(ItemBindState.Unbound, new(0, 0), null, "Test", false));
    private static TradeItemPolicy Policy(bool direct = true, bool auction = true) => new([new("Material", "Test.Ore", 1, direct, auction, 999)]);
    private static void Validate(ItemRecord item, TradeItemPolicy? policy = null, TradeChannel channel = TradeChannel.Direct,
        int quantity = 30, long revision = 7, TradeParticipant? seller = null) =>
        (policy ?? Policy()).ValidateOfferItem(item, seller ?? Alice, new(item.ItemId, revision, quantity), channel, Now);

    private static void DefinitionAndOwnership()
    {
        ItemRecord item = Item();
        Validate(item); assertions++;
        Validate(item, channel: TradeChannel.Auction); assertions++;
        Reject(() => Validate(item, new([])), "item_trade_not_allowed");
        Reject(() => Validate(item with { DefinitionVersion = 2 }), "item_trade_not_allowed");
        Reject(() => Validate(item, Policy(false, true)), "item_trade_not_allowed");
        Reject(() => Validate(item, Policy(true, false), TradeChannel.Auction), "item_trade_not_allowed");
        Reject(() => Validate(item, seller: Bob), "item_not_in_owned_inventory");
        Reject(() => Validate(item with { Owner = new(ItemOwnerType.Account, Alice.AccountId) }), "item_not_in_owned_inventory");
        foreach (ItemContainerType location in new[] { ItemContainerType.Mail, ItemContainerType.Equipment, ItemContainerType.Auction, ItemContainerType.Trade })
            Reject(() => Validate(item with { Location = item.Location with { ContainerType = location } }), "item_not_in_owned_inventory");
        Reject(() => Validate(item with { Location = item.Location with { ContainerId = Bob.CharacterId.ToString("D") } }), "item_not_in_owned_inventory");
        Reject(() => Validate(item, channel: (TradeChannel)99), "invalid_trade_item");
        TradeDefinitionPolicy rule = new("Material", "Test.Ore", 1, true, true, 999);
        Reject(() => new TradeItemPolicy([rule, rule]), "invalid_trade_catalog");
        Reject(() => new TradeItemPolicy([rule with { MaximumStackSize = 0 }]), "invalid_trade_catalog");
        TradeDefinitionPolicy[] source = [rule];
        TradeItemPolicy frozen = new(source);
        source[0] = rule with { AllowDirect = false };
        Validate(item, frozen); assertions++;
        Console.WriteLine("PASS deny-by-default/versioned catalog, channels and inventory ownership");
    }

    private static void ItemBoundaries()
    {
        ItemRecord item = Item();
        string before = JsonSerializer.Serialize(item);
        foreach (ItemBindState bound in new[] { ItemBindState.CharacterBound, ItemBindState.AccountBound })
            Reject(() => Validate(item with { Metadata = item.Metadata with { BindState = bound } }), "item_bound");
        Reject(() => Validate(item with { Metadata = item.Metadata with { BindState = (ItemBindState)99 } }), "invalid_trade_item");
        Validate(item with { Metadata = item.Metadata with { BindState = ItemBindState.BindOnEquip } }); assertions++;
        Reject(() => Validate(item with { Metadata = item.Metadata with { IsLocked = true } }), "item_unavailable");
        Reject(() => Validate(item with { Metadata = item.Metadata with { ExpiresAtUtc = Now } }), "item_unavailable");
        Validate(item with { Metadata = item.Metadata with { ExpiresAtUtc = Now.AddTicks(1) } }); assertions++;
        foreach (long revision in new long[] { 0, 6, 8, long.MaxValue })
            Reject(() => Validate(item, revision: revision), "item_revision_conflict");
        foreach (int quantity in new[] { 0, -1, 101 })
            Reject(() => Validate(item, quantity: quantity), "invalid_trade_quantity");
        Reject(() => Validate(item with { State = item.State with { Quantity = 1000 } }), "invalid_trade_quantity");
        Reject(() => Validate(item with { State = item.State with { Quantity = 0 } }), "invalid_trade_item");
        Reject(() => Policy().ValidateOfferItem(item, Alice, new(Guid.NewGuid(), 7, 1), TradeChannel.Direct, Now), "item_revision_conflict");
        Check(JsonSerializer.Serialize(item) == before, "Policy never mutates inventory.");
        Console.WriteLine("PASS bind, lock, expiry, revision, quantity and non-mutating policy checks");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        assertions++;
    }
    private static void Reject(Action action, string code)
    {
        try { action(); }
        catch (TradeRuleException ex) when (ex.Code == code) { assertions++; return; }
        throw new Exception("Expected rejection: " + code);
    }
}
