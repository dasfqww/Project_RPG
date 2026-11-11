using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

// Process-local implementation of the same internal commands as PostgreSQL. Not restart persistence.
public sealed partial class InMemoryAuctionRepository : IAuctionEscrowRepository, IAuctionPurchaseRepository, IAuctionClaimRepository
{
    private readonly InMemoryTransactionGate gate;
    private readonly InMemoryGameRepository game;
    private readonly InMemoryItemRepository items;
    private readonly InMemoryEconomyRepository economy;
    private readonly TradeItemPolicy policy;
    private readonly AuctionEscrowLimits limits;
    public InMemoryAuctionRepository(InMemoryTransactionGate gate, InMemoryGameRepository game, InMemoryItemRepository items,
        InMemoryEconomyRepository economy, TradeItemPolicy policy, AuctionEscrowLimits limits)
    {
        game.ValidateTradeAssets(gate, items);
        if (!ReferenceEquals(gate.SyncRoot, economy.TransactionSyncRoot)) throw new InvalidOperationException("Auction requires the shared currency gate.");
        this.gate = gate; this.game = game; this.items = items; this.economy = economy; this.policy = policy; this.limits = limits;
    }
    public Task<AuctionEscrowListing?> FindAsync(Guid id, TradeParticipant seller, CancellationToken ct) => Read(() =>
    { var listing = Load(id); return listing?.State.Seller == seller ? listing : null; }, ct);

    public Task<AuctionSellerPayable?> FindPayableAsync(Guid id, TradeParticipant actor, CancellationToken ct) => Read(() =>
    {
        if (id == Guid.Empty || !AuctionTerms.ValidParticipant(actor)) throw new TradeRuleException("invalid_auction_claim");
        var context = Context(actor, ct); var p = Payable(id);
        return p is not null && Authorized(p, actor, context) ? p : null;
    }, ct);

    public Task<AuctionEscrowResult> ExecuteAsync(AuctionEscrowCommand cmd, CancellationToken ct)
    {
        cmd = AuctionEscrowRules.Validate(cmd); var fingerprint = AuctionEscrowRules.Fingerprint(cmd);
        return Atomic(() =>
        {
            var receipt = Receipt<AuctionEscrowResult>(cmd.RequestId, "escrow", fingerprint);
            if (receipt is not null) return receipt;
            var before = Load(cmd.ListingId);
            if (before is not null && before.State.Seller != cmd.Seller) throw new TradeRuleException("auction_seller_required");
            if (cmd.Kind == AuctionEscrowKind.Register && before is not null) throw new TradeRuleException("auction_already_exists");
            if (cmd.Kind != AuctionEscrowKind.Register && before is null) throw new TradeRuleException("auction_not_found");
            Context(cmd.Seller, ct); var owner = Owner(cmd.Seller); var now = gate.UtcNow;
            AuctionEscrowListing listing; List<ItemRecordMutation> changes;
            if (cmd.Kind == AuctionEscrowKind.Register)
            {
                game.EnsureAuctionAccess([cmd.Seller], cmd.Seller, now);
                if (gate.AuctionListings.Keys.Select(id => Load(id)!).Count(l => l.State.Seller.AccountId == cmd.Seller.AccountId && l.State.Status == AuctionListingStatus.Active) >= limits.MaximumOpenListingsPerAccount)
                    throw new TradeRuleException("auction_listing_limit");
                var definition = Definition(cmd.Terms!.CurrencyCode, ct);
                var source = items.FindAsync(cmd.Item!.ItemId, ct).GetAwaiter().GetResult();
                if (source is null || source.Owner != owner) throw new TradeRuleException("item_not_in_owned_inventory");
                if (gate.IsItemReserved(source.ItemId)) throw new TradeRuleException("auction_item_reserved");
                var state = AuctionListing.Prepare(cmd.ListingId, cmd.Seller, source, cmd.Item, cmd.TotalPrice, now, cmd.ExpiresAt, cmd.Terms, policy);
                bool split = cmd.Item.Quantity < source.State.Quantity; Guid id = split ? AuctionEscrowRules.SplitItemId(cmd.ListingId) : source.ItemId;
                if (split && items.FindAsync(id, ct).GetAwaiter().GetResult() is not null) throw new TradeRuleException("auction_identity_conflict");
                var escrow = source with { Owner = AuctionEscrowRules.Owner(cmd.ListingId), Location = new(ItemContainerType.Auction, cmd.ListingId.ToString("D"), 0),
                    State = source.State with { InstanceId = id, Quantity = cmd.Item.Quantity }, Revision = split ? 0 : source.Revision };
                changes = [new(escrow.Revision, escrow)];
                if (split) changes.Add(new(source.Revision, source with { State = source.State with { Quantity = source.State.Quantity - cmd.Item.Quantity } }));
                listing = new(state, ItemRecordRules.SnapshotWithRevision(escrow, escrow.Revision + 1), split ? new(source.ItemId, id, cmd.Item.Quantity) : null, policy.MaximumStackSize(source, TradeChannel.Auction));
            }
            else
            {
                var state = cmd.Kind == AuctionEscrowKind.Cancel ? before!.State.Cancel(cmd.Seller, cmd.ExpectedRevision, now) : before!.State.Expire(cmd.ExpectedRevision, now);
                changes = Deliver(cmd.RequestId, before, Escrow(before, ct), owner, now, ct, "auction_return_capacity"); listing = before with { State = state };
            }
            AuctionEscrowRules.Validate(listing); var written = Write(changes);
            gate.AuctionListings[cmd.ListingId] = Json(listing);
            return Save(cmd.RequestId, "escrow", fingerprint, new AuctionEscrowResult(cmd.RequestId, listing, written, now));
        }, ct);
    }

    public Task<AuctionPurchaseResult> PurchaseAsync(AuctionPurchaseCommand cmd, CancellationToken ct)
    {
        AuctionPurchaseRules.Validate(cmd); var fingerprint = AuctionPurchaseRules.Fingerprint(cmd);
        return Atomic(() =>
        {
            var receipt = Receipt<AuctionPurchaseResult>(cmd.RequestId, "purchase", fingerprint);
            if (receipt is not null) return receipt;
            var l = Load(cmd.ListingId) ?? throw new TradeRuleException("auction_not_found");
            l.State.AgreePurchase(cmd.Buyer, cmd.ExpectedRevision, cmd.ExpectedCurrencyCode, cmd.ExpectedTotalPrice, gate.UtcNow);
            var seller = Context(l.State.Seller, ct); var buyer = Context(cmd.Buyer, ct);
            game.EnsureAuctionAccess([l.State.Seller, cmd.Buyer], cmd.Buyer, gate.UtcNow);
            var definition = Definition(cmd.ExpectedCurrencyCode, ct);
            var balance = Balance(buyer, definition.CurrencyCode, ct);
            var sellerWallet = new CurrencyOwnerRef(definition.Scope, EconomyRules.ResolveOwnerId(definition.Scope, seller));
            if (balance.Owner == sellerWallet) throw new TradeRuleException("auction_self_purchase");
            long reserved = gate.ReservedCurrency(balance.Owner, definition.CurrencyCode);
            if (reserved < 0 || reserved > balance.Balance || cmd.ExpectedTotalPrice > balance.Balance - reserved) throw new TradeRuleException("auction_insufficient_balance");
            if (balance.Revision == long.MaxValue) throw new TradeRuleException("auction_wallet_revision_exhausted");
            var escrow = Escrow(l, ct); policy.ValidateAuctionEscrow(escrow, l, gate.UtcNow);
            var delivery = l with { MaximumStackSize = Math.Min(l.MaximumStackSize, policy.MaximumStackSize(escrow, TradeChannel.Auction)) };
            var changes = Deliver(cmd.RequestId, delivery, escrow, Owner(cmd.Buyer), gate.UtcNow, ct, "auction_delivery_capacity");
            var now = gate.UtcNow; var plan = l.State.AgreePurchase(cmd.Buyer, cmd.ExpectedRevision, cmd.ExpectedCurrencyCode, cmd.ExpectedTotalPrice, now);
            policy.ValidateAuctionEscrow(escrow, l, now);
            var sold = l with { State = plan.Listing, Sale = new(cmd.RequestId, cmd.Buyer, definition, sellerWallet, plan.Amounts, now) };
            var written = Write(changes); long next = balance.Balance - cmd.ExpectedTotalPrice;
            economy.ApplyTradePlan([new(balance.Owner, definition.CurrencyCode, balance.Revision, balance.Balance, next)]);
            l.State.AgreePurchase(cmd.Buyer, cmd.ExpectedRevision, cmd.ExpectedCurrencyCode, cmd.ExpectedTotalPrice, gate.UtcNow);
            var result = new AuctionPurchaseResult(cmd.RequestId, sold, new(balance.Owner, balance.Balance, next, balance.Revision), written, now);
            AuctionPurchaseRules.Validate(result, cmd);
            gate.AuctionListings[cmd.ListingId] = Json(sold);
            gate.AuctionPayables.Add(cmd.ListingId, Json(new AuctionSellerPayable(cmd.ListingId, l.State.Seller, sellerWallet, definition.CurrencyCode, plan.Amounts.SellerPayable, plan.Amounts.SellerPayable, 1)));
            return Save(cmd.RequestId, "purchase", fingerprint, result);
        }, ct);
    }

    public Task<AuctionClaimResult> ClaimAsync(AuctionClaimCommand cmd, CancellationToken ct)
    {
        AuctionClaimRules.Validate(cmd); var fingerprint = AuctionClaimRules.Fingerprint(cmd);
        return Atomic(() =>
        {
            var receipt = Receipt<AuctionClaimResult>(cmd.RequestId, "claim", fingerprint);
            if (receipt is not null) return receipt;
            var context = Context(cmd.Claimant, ct); var p = Payable(cmd.ListingId) ?? throw new TradeRuleException("auction_payable_not_found");
            if (!Authorized(p, cmd.Claimant, context)) throw new TradeRuleException("auction_payable_owner_required");
            if (p.Revision != cmd.ExpectedRevision) throw new TradeRuleException("auction_payable_revision_conflict");
            if (p.CurrencyCode != cmd.ExpectedCurrencyCode) throw new TradeRuleException("auction_currency_conflict");
            game.EnsureAuctionAccess([cmd.Claimant], cmd.Claimant, gate.UtcNow);
            var definition = Definition(p.CurrencyCode, ct);
            if (definition.Scope != p.Wallet.Scope) throw new TradeRuleException("auction_payable_owner_required");
            var balance = Balance(context, p.CurrencyCode, ct);
            var amounts = AuctionMoney.Claim(p.RemainingAmount, cmd.RequestedAmount, balance.Balance, definition.MaxBalance);
            if (amounts.Paid > 0)
            {
                if (balance.Revision == long.MaxValue) throw new TradeRuleException("auction_wallet_revision_exhausted");
                economy.ApplyTradePlan([new(p.Wallet, p.CurrencyCode, balance.Revision, balance.Balance, amounts.NewWalletBalance)]);
            }
            var after = p with { RemainingAmount = amounts.RemainingPayable, Revision = p.Revision + (amounts.Paid > 0 ? 1 : 0) };
            var result = new AuctionClaimResult(cmd.RequestId, cmd.Claimant, p, after, new(definition, balance.Balance, amounts.NewWalletBalance, balance.Revision), amounts.Paid, gate.UtcNow);
            AuctionClaimRules.Validate(result, cmd); gate.AuctionPayables[p.ListingId] = Json(after);
            return Save(cmd.RequestId, "claim", fingerprint, result);
        }, ct);
    }

    private List<ItemRecordMutation> Deliver(Guid request, AuctionEscrowListing l, ItemRecord escrow, ItemOwnerRef owner, DateTimeOffset now, CancellationToken ct, string capacity)
    {
        ItemRecord Terminal(ItemLifecycleState state) => escrow with { State = escrow.State with { Quantity = 0 }, Location = new(ItemContainerType.Terminal, "", -1), LifecycleState = state };
        if (escrow.Metadata.ExpiresAtUtc is { } expiry && expiry <= now) return [new(escrow.Revision, Terminal(ItemLifecycleState.Expired))];
        var inventory = items.TradeInventory(owner);
        if (inventory.Count > InventoryBagLayout.TotalSlots || inventory.Any(i => !ItemRecordRules.TryValidateRecord(i, out _) || !InventoryBagLayout.CanStore(i))
            || inventory.Select(i => i.Location.SlotIndex).Distinct().Count() != inventory.Count) throw new TradeRuleException("invalid_auction_inventory");
        var virtualArrival = escrow with { Owner = owner, Location = new(ItemContainerType.Mail, l.State.ListingId.ToString("D"), 0), Revision = 0 };
        var grant = new ItemRepositoryCommitRequest(request, "AuctionDelivery", "auction-delivery", owner, escrow.State.Quantity, [new(0, virtualArrival)]);
        var held = inventory.Where(i => gate.IsItemReserved(i.ItemId)).Select(i => i.ItemId).ToHashSet();
        var changes = DungeonRewardDeliveryPlanner.PlanAuctionReturn(grant, [l.MaximumStackSize], inventory, held).Mutations.Select(m => m.NewRecord.ItemId == escrow.ItemId
            ? new ItemRecordMutation(escrow.Revision, m.NewRecord with { Revision = escrow.Revision }) : m).ToList();
        if (!changes.Any(m => m.NewRecord.ItemId == escrow.ItemId)) changes.Add(new(escrow.Revision, Terminal(ItemLifecycleState.Consumed)));
        else if (items.FindByOwnerAsync(owner, false, 501, ct).GetAwaiter().GetResult().Count >= limits.MaximumOwnedRecords) throw new TradeRuleException(capacity);
        return changes;
    }
    private ItemRecord[] Write(List<ItemRecordMutation> changes)
    { items.ApplyTradePlan(changes); return changes.Select(m => ItemRecordRules.SnapshotWithRevision(m.NewRecord, m.ExpectedRevision + 1)).ToArray(); }
    private ItemRecord Escrow(AuctionEscrowListing l, CancellationToken ct)
    { var item = items.FindAsync(l.EscrowAtRegistration.ItemId, ct).GetAwaiter().GetResult(); return item is not null && Json(item) == Json(l.EscrowAtRegistration) ? item : throw new TradeRuleException("auction_escrow_changed"); }
    private AuctionEscrowListing? Load(Guid id)
    { if (!gate.AuctionListings.TryGetValue(id, out var json)) return null; var l = Decode<AuctionEscrowListing>(json); AuctionEscrowRules.Validate(l); return l; }
    private AuctionSellerPayable? Payable(Guid id)
    {
        if (!gate.AuctionPayables.TryGetValue(id, out var json)) return null;
        var p = Decode<AuctionSellerPayable>(json); AuctionClaimRules.Validate(p); var l = Load(id);
        if (l?.Sale is null || p.Seller != l.State.Seller || p.Wallet != l.Sale.SellerWallet || p.CurrencyCode != l.Sale.Currency.CurrencyCode || p.InitialAmount != l.Sale.Amounts.SellerPayable)
            throw new InvalidDataException("Payable does not match sale.");
        return p;
    }
    private CharacterEconomyContext Context(TradeParticipant p, CancellationToken ct)
    { var c = game.GetCharacterEconomyContextAsync(p.CharacterId, ct).GetAwaiter().GetResult(); return c?.SteamId == p.AccountId ? c : throw new TradeRuleException("character_not_owned"); }
    private CurrencyDefinition Definition(string code, CancellationToken ct)
    { var d = economy.GetDefinitionsAsync(ct).GetAwaiter().GetResult().SingleOrDefault(d => d.CurrencyCode == code); return d is not null && d.Enabled && EconomyRules.TryValidateDefinition(d, out _) ? d : throw new TradeRuleException("auction_currency_unavailable"); }
    private CurrencyBalance Balance(CharacterEconomyContext c, string code, CancellationToken ct) => economy.LoadWalletAsync(c, ct).GetAwaiter().GetResult().Balances.Single(b => b.Definition.CurrencyCode == code);
    private static ItemOwnerRef Owner(TradeParticipant p) => new(ItemOwnerType.Character, p.CharacterId.ToString("D"));
    private static bool Authorized(AuctionSellerPayable p, TradeParticipant actor, CharacterEconomyContext c) => p.Seller.AccountId == actor.AccountId && p.Wallet.OwnerId == EconomyRules.ResolveOwnerId(p.Wallet.Scope, c);
    private static string Json<T>(T value) => JsonSerializer.Serialize(value);
    private static T Decode<T>(string json) => JsonSerializer.Deserialize<T>(json) ?? throw new InvalidDataException("Invalid auction JSON.");
    private T? Receipt<T>(Guid id, string kind, string fingerprint) where T : class
    { if (!gate.AuctionReceipts.TryGetValue(id, out var r)) return null; if (r.Kind != kind || r.Fingerprint != fingerprint) throw new TradeRuleException("auction_idempotency_conflict"); return Decode<T>(r.Result); }
    private T Save<T>(Guid id, string kind, string fingerprint, T result)
    { var json = Json(result); gate.AuctionReceipts.Add(id, (kind, fingerprint, json)); return Decode<T>(json); }
    private Task<T> Read<T>(Func<T> action, CancellationToken ct) => Task.FromResult(game.WithTradeLocks(gate, () => { ct.ThrowIfCancellationRequested(); return action(); }));
    private Task<T> Atomic<T>(Func<T> action, CancellationToken ct) => Read(() =>
    {
        var itemState = items.CaptureSnapshot(); var money = economy.CaptureSnapshot();
        var listings = new Dictionary<Guid, string>(gate.AuctionListings); var payables = new Dictionary<Guid, string>(gate.AuctionPayables);
        var receipts = new Dictionary<Guid, (string Kind, string Fingerprint, string Result)>(gate.AuctionReceipts);
        try { var result = action(); ct.ThrowIfCancellationRequested(); return result; }
        catch { items.RestoreSnapshot(itemState); economy.RestoreSnapshot(money); Restore(gate.AuctionListings, listings); Restore(gate.AuctionPayables, payables); Restore(gate.AuctionReceipts, receipts); throw; }
    }, ct);
    private static void Restore<T>(Dictionary<Guid, T> target, Dictionary<Guid, T> snapshot)
    { target.Clear(); foreach (var entry in snapshot) target.Add(entry.Key, entry.Value); }
}
