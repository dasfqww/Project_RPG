using System.Text.Json;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Shop;

/// <summary>Uses the same dungeon, item and wallet gates as admission and all existing asset commands.</summary>
public sealed class InMemoryShopStore : IShopStore
{
    private readonly InMemoryTransactionGate gate;
    private readonly InMemoryGameRepository game;
    private readonly InMemoryItemRepository items;
    private readonly InMemoryEconomyRepository economy;

    public InMemoryShopStore(InMemoryTransactionGate gate, InMemoryGameRepository game,
        InMemoryItemRepository items, InMemoryEconomyRepository economy)
    {
        game.ValidateTradeAssets(gate, items);
        if (!ReferenceEquals(gate.SyncRoot, economy.TransactionSyncRoot))
            throw new InvalidOperationException("Shop requires the shared wallet transaction gate.");
        this.gate = gate; this.game = game; this.items = items; this.economy = economy;
    }

    public Task<ShopInventorySnapshot> ReadAsync(ShopActor actor, ShopCatalog catalog, CancellationToken ct) => Read(() =>
    {
        var context = Context(actor, ct);
        if (!catalog.Enabled) throw new ShopRuleException("shop_disabled");
        var wallet = Wallet(context, ct);
        var balance = wallet.Balances.SingleOrDefault(b => b.Definition.CurrencyCode == catalog.CurrencyCode);
        if (balance is null || !balance.Definition.Enabled) throw new ShopRuleException("shop_currency_unavailable");
        var held = gate.ReservedCurrency(balance.Owner, catalog.CurrencyCode);
        if (held < 0 || held > balance.Balance) throw new ShopRuleException("shop_assets_unavailable");
        var owned = Owned(actor, ct);
        return new ShopInventorySnapshot(actor, balance, balance.Balance - held, owned,
            owned.Where(i => gate.IsItemReserved(i.ItemId)).Select(i => i.ItemId).ToHashSet(), gate.UtcNow);
    }, ct);

    public Task<ShopReceipt> ExecuteAsync(ShopCommand command, ShopCatalog catalog, CancellationToken ct)
    {
        ShopRules.Validate(command);
        var fingerprint = ShopRules.Fingerprint(command);
        return Read(() =>
        {
            var context = Context(command.Actor, ct);
            if (gate.ShopReceipts.TryGetValue(command.RequestId, out var previous))
            {
                if (previous.Fingerprint != fingerprint) throw new ShopRuleException("shop_idempotency_conflict");
                return Receipt(previous.Command, previous.Result, command);
            }
            var itemSnapshot = items.CaptureSnapshot(); var moneySnapshot = economy.CaptureSnapshot();
            bool assetWriteStarted = false;
            try
            {
                if (!catalog.Enabled) throw new ShopRuleException("shop_disabled");
                game.EnsureShopAccess(command.Actor, gate.UtcNow);
                var definition = economy.GetDefinitionsAsync(ct).GetAwaiter().GetResult()
                    .SingleOrDefault(d => d.CurrencyCode == catalog.CurrencyCode);
                if (definition is null || !definition.Enabled || !EconomyRules.TryValidateDefinition(definition, out _))
                    throw new ShopRuleException("shop_currency_unavailable");
                var balance = Wallet(context, ct).Balances.Single(b => b.Definition.CurrencyCode == catalog.CurrencyCode);
                if (balance.Revision == long.MaxValue) throw new ShopRuleException("shop_revision_exhausted");
                var owned = Owned(command.Actor, ct);
                var inventory = owned.Where(i => i.Location.ContainerType == ItemContainerType.Inventory).ToArray();
                var reserved = inventory.Where(i => gate.IsItemReserved(i.ItemId)).Select(i => i.ItemId).ToHashSet();
                long held = gate.ReservedCurrency(balance.Owner, catalog.CurrencyCode);
                var now = gate.UtcNow;
                var plan = ShopPlanner.Plan(command, catalog, inventory, reserved, balance.Balance,
                    balance.Balance - held, definition.MaxBalance, now);
                int newRecords = plan.Mutations.Count(m => m.ExpectedRevision == 0);
                if (owned.Count + newRecords > 500) throw new ShopRuleException("shop_inventory_full");
                assetWriteStarted = true;
                items.ApplyTradePlan(plan.Mutations);
                economy.ApplyTradePlan([new(balance.Owner, catalog.CurrencyCode, balance.Revision, balance.Balance, plan.BalanceAfter)]);
                var written = plan.Mutations.Select(m => ItemRecordRules.SnapshotWithRevision(m.NewRecord, m.ExpectedRevision + 1)).ToArray();
                var result = new ShopReceipt(command.RequestId, command.Actor, command.ShopId, command.PolicyTag,
                    command.Action, command.OfferId, command.ItemId, command.Quantity, command.ExpectedUnitPrice,
                    plan.TotalPrice, catalog.CurrencyCode, balance.Balance, plan.BalanceAfter, written, now);
                ShopRules.ValidateReceipt(result, command);
                string payload = JsonSerializer.Serialize(result), original = JsonSerializer.Serialize(command);
                ct.ThrowIfCancellationRequested();
                gate.ShopReceipts.Add(command.RequestId, (fingerprint, original, payload));
                return Receipt(original, payload, command);
            }
            catch (ShopRuleException error) when (!assetWriteStarted && ShopRejectionRules.IsDurable(error.Code))
            {
                items.RestoreSnapshot(itemSnapshot); economy.RestoreSnapshot(moneySnapshot);
                var rejection = new ShopRejection(error.Code, gate.UtcNow);
                ShopRejectionRules.Validate(rejection);
                string payload = JsonSerializer.Serialize(rejection), original = JsonSerializer.Serialize(command);
                ct.ThrowIfCancellationRequested();
                gate.ShopReceipts.Add(command.RequestId, (fingerprint, original, payload));
                throw new ShopRejectedException(command, rejection.Error, rejection.RejectedAt);
            }
            catch
            {
                gate.ShopReceipts.Remove(command.RequestId);
                items.RestoreSnapshot(itemSnapshot); economy.RestoreSnapshot(moneySnapshot); throw;
            }
        }, ct);
    }

    public Task<ShopReceipt?> FindAsync(ShopActor actor, Guid requestId, CancellationToken ct) => Read(() =>
    {
        Context(actor, ct); return Find(actor, requestId);
    }, ct);

    public Task<ShopAssets?> AssetsAsync(ShopActor actor, Guid requestId, CancellationToken ct) => Read(() =>
    {
        var context = Context(actor, ct);
        if (Find(actor, requestId) is null) return null;
        return new ShopAssets(requestId, actor.CharacterId, gate.UtcNow, Wallet(context, ct), Owned(actor, ct));
    }, ct);

    private ShopReceipt? Find(ShopActor actor, Guid requestId)
    {
        if (!gate.ShopReceipts.TryGetValue(requestId, out var stored)) return null;
        var command = JsonSerializer.Deserialize<ShopCommand>(stored.Command) ?? throw new InvalidDataException("Missing shop command.");
        if (command.Actor != actor) return null;
        if (command.RequestId != requestId || ShopRules.Fingerprint(command) != stored.Fingerprint)
            throw new InvalidDataException("Shop receipt index mismatch.");
        return Receipt(stored.Command, stored.Result, command);
    }
    private static ShopReceipt Receipt(string original, string payload, ShopCommand command)
    {
        if (JsonSerializer.Deserialize<ShopCommand>(original) != command) throw new InvalidDataException("Shop command mismatch.");
        return ShopStoredOutcome.Decode(payload, command).Replay(command);
    }
    private CharacterEconomyContext Context(ShopActor actor, CancellationToken ct)
    {
        if (!ShopRules.ValidActor(actor)) throw new ShopRuleException("invalid_shop_actor");
        var context = game.GetCharacterEconomyContextAsync(actor.CharacterId, ct).GetAwaiter().GetResult();
        return context?.SteamId == actor.AccountId ? context : throw new ShopRuleException("shop_character_not_owned");
    }
    private CurrencyWallet Wallet(CharacterEconomyContext context, CancellationToken ct) =>
        economy.LoadWalletAsync(context, ct).GetAwaiter().GetResult();
    private IReadOnlyList<ItemRecord> Owned(ShopActor actor, CancellationToken ct)
    {
        var result = items.FindByOwnerAsync(new(ItemOwnerType.Character, actor.CharacterId.ToString("D")), false, 501, ct).GetAwaiter().GetResult();
        if (result.Count > 500) throw new ShopRuleException("shop_assets_unavailable");
        return result.Select(i => ItemRecordRules.SnapshotWithRevision(i, i.Revision)).ToArray();
    }
    private Task<T> Read<T>(Func<T> action, CancellationToken ct) => Task.FromResult(game.WithTradeLocks(gate,
        () => { ct.ThrowIfCancellationRequested(); return action(); }));
}
