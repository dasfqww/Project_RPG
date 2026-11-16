using System.Text.Json;
using Npgsql;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Shop;

namespace ProjectRpg.Backend.ShopTests;

public static class StorageCases
{
    public static async Task Run(Action<bool, string> assert, NpgsqlDataSource? dataSource = null)
    {
        var f = await Fixture.Create(assert, dataSource);
        async Task Reject(string code, Func<Task> action)
        {
            try { await action(); assert(false, "expected " + code); }
            catch (ShopRuleException e) { assert(e.Code == code, "rejected " + code + ": " + e.Code); }
        }
        async Task<ShopRejectedException> Denied(ShopCommand command, string code, Func<Task> action)
        {
            try { await action(); }
            catch (ShopRejectedException e)
            {
                assert(e.Code == code && e.Command == command && e.RejectedAt != default,
                    "durable rejection binds the original command: " + code);
                return e;
            }
            throw new InvalidOperationException("Expected durable shop rejection: " + code);
        }
        async Task<string> State(ShopActor actor) => JsonSerializer.Serialize(new
        { Balance = await f.Balance(actor), Items = (await f.Owned(actor)).OrderBy(i => i.ItemId).ToArray() });
        async Task NoChange(ShopActor actor, ShopCommand command, string error)
        {
            string before = await State(actor);
            var denied = await Denied(command, error, () => f.Store.ExecuteAsync(command, f.Catalog, default));
            var found = await Denied(command, error, () => f.Store.FindAsync(actor, command.RequestId, default));
            var assets = await Denied(command, error, () => f.Store.AssetsAsync(actor, command.RequestId, default));
            assert(await State(actor) == before && denied.RejectedAt == found.RejectedAt && denied.RejectedAt == assets.RejectedAt,
                "failed shop order preserves assets and a recoverable immutable rejection: " + error);
        }

        var buyer = await f.Person(); var order = f.Buy(buyer);
        var result = await f.Store.ExecuteAsync(order, f.Catalog, default);
        assert(result.BalanceBefore == 1000 && result.BalanceAfter == 980 && await f.Balance(buyer) == 980,
            "purchase persists exactly one debit");
        assert((await f.Owned(buyer)).Where(i => i.DefinitionName == Fixture.Potion.DefinitionName).Sum(i => i.State.Quantity) == 2,
            "purchase persists exact consumable quantity");
        string receipt = JsonSerializer.Serialize(result);
        assert(JsonSerializer.Serialize(await f.Store.ExecuteAsync(order, f.Catalog, default)) == receipt
            && await f.Balance(buyer) == 980, "same request replays immutable receipt");
        var read = await f.Store.ReadAsync(buyer, f.Catalog, default);
        assert(read.AvailableBalance == 980 && read.Balance.Balance == 980, "shop snapshot has current spendable funds");
        var assets = await f.Store.AssetsAsync(buyer, order.RequestId, default);
        assert(assets is not null && assets.CharacterId == buyer.CharacterId
            && assets.Items.Any(i => i.ItemId == result.Records[0].ItemId), "confirmed request authorizes owned fresh assets");
        if (result.Records is IList<ItemRecord> mutable)
            mutable[0] = mutable[0] with { State = mutable[0].State with { Quantity = 999 } };
        assert(JsonSerializer.Serialize(await f.Store.FindAsync(buyer, order.RequestId, default)) == receipt,
            "mutating returned receipt cannot mutate stored evidence");
        var second = await f.Store.ExecuteAsync(f.Buy(buyer), f.Catalog, default);
        assert(second.Records.Count == 1 && second.Records[0].Revision == 2 && second.Records[0].State.Quantity == 4,
            "subsequent purchase merges into existing compatible stack");
        var disabled = new ShopCatalog(false, "", "", "", [], []);
        assert(JsonSerializer.Serialize(await f.Store.ExecuteAsync(order, disabled, default)) == receipt,
            "old receipt replays after catalog disable/removal");
        var changed = new ShopCatalog(true, f.Catalog.ShopId, "가격 변경", f.Catalog.CurrencyCode,
            [Fixture.Potion with { UnitPrice = 11 }], []);
        assert(JsonSerializer.Serialize(await f.Store.ExecuteAsync(order, changed, default)) == receipt,
            "old receipt replays after price and policy change");
        await Reject("shop_idempotency_conflict", () => f.Store.ExecuteAsync(order with { Quantity = 1 }, f.Catalog, default));
        var stranger = await f.Person(); var alt = await f.Person(account: buyer.AccountId);
        assert(await f.Store.FindAsync(stranger, order.RequestId, default) is null
            && await f.Store.FindAsync(alt, order.RequestId, default) is null
            && await f.Store.AssetsAsync(stranger, order.RequestId, default) is null, "receipt/assets isolate character and account");
        await Reject("shop_character_not_owned", () => f.Store.ExecuteAsync(order with { Actor = buyer with { AccountId = stranger.AccountId } }, f.Catalog, default));

        var sealedActor = await f.Person(); var sealedOrder = f.Buy(sealedActor);
        string sealedBefore = await State(sealedActor);
        var sealedDenial = await Denied(sealedOrder, "shop_disabled", () => f.Store.ExecuteAsync(sealedOrder, disabled, default));
        var retry = await Denied(sealedOrder, "shop_disabled", () => f.Store.ExecuteAsync(sealedOrder, f.Catalog, default));
        assert(retry.RejectedAt == sealedDenial.RejectedAt && await State(sealedActor) == sealedBefore,
            "re-enabling a shop never commits a previously rejected request");
        await Reject("shop_idempotency_conflict", () => f.Store.ExecuteAsync(sealedOrder with { Quantity = 1 }, f.Catalog, default));
        var afterConflict = await Denied(sealedOrder, "shop_disabled", () => f.Store.FindAsync(sealedActor, sealedOrder.RequestId, default));
        assert(afterConflict.RejectedAt == sealedDenial.RejectedAt
            && await f.Store.FindAsync(stranger, sealedOrder.RequestId, default) is null
            && await f.Store.AssetsAsync(stranger, sealedOrder.RequestId, default) is null,
            "rejection cannot be overwritten or disclosed to another owner");
        await f.Store.ExecuteAsync(f.Buy(sealedActor), f.Catalog, default);
        assert(await f.Balance(sealedActor) == 980, "a new request can succeed after a prior terminal rejection");

        var malformed = f.Buy(sealedActor);
        await Reject("invalid_shop_command", () => f.Store.ExecuteAsync(malformed with { Quantity = 0 }, f.Catalog, default));
        await Reject("shop_character_not_owned", () => f.Store.ExecuteAsync(malformed with { Actor = sealedActor with { AccountId = stranger.AccountId } }, f.Catalog, default));
        assert(await f.Store.FindAsync(sealedActor, malformed.RequestId, default) is null,
            "invalid commands and foreign context do not create a terminal outcome");
        await f.Store.ExecuteAsync(malformed, f.Catalog, default);
        assert(await f.Balance(sealedActor) == 960, "valid command is not poisoned by malformed or foreign attempts");

        var seller = await f.Person(); var junk = await f.Seed(seller, Fixture.Junk.DefinitionName, 160);
        var sale = f.Sell(seller, junk); var saleResult = await f.Store.ExecuteAsync(sale, f.Catalog, default);
        assert(saleResult.BalanceAfter == 1004 && (await f.Items.FindAsync(junk.ItemId, default))!.State.Quantity == 2,
            "sale atomically consumes part of stack and credits funds");
        assert(JsonSerializer.Serialize(saleResult) == JsonSerializer.Serialize(await f.Store.ExecuteAsync(sale, f.Catalog, default))
            && await f.Balance(seller) == 1004, "sale replay has no duplicate credit");
        var remainder = (await f.Items.FindAsync(junk.ItemId, default))!;
        await f.Store.ExecuteAsync(f.Sell(seller, remainder), f.Catalog, default);
        var tombstone = (await f.Items.FindAsync(junk.ItemId, default))!;
        assert(tombstone.LifecycleState == ItemLifecycleState.Consumed && tombstone.State.Quantity == 0
            && await f.Balance(seller) == 1008, "whole sale retains consumed item and exact credit");
        assert(JsonSerializer.Serialize(await f.Store.ExecuteAsync(sale, disabled, default)) == JsonSerializer.Serialize(saleResult),
            "historical sale survives later consumption and disabled policy");

        var poor = await f.Person(19); var unaffordable = f.Buy(poor);
        await NoChange(poor, unaffordable, "shop_insufficient_balance");
        await f.Fund(poor, 100);
        string fundedBeforeRetry = await State(poor);
        await Denied(unaffordable, "shop_insufficient_balance", () => f.Store.ExecuteAsync(unaffordable, f.Catalog, default));
        assert(await f.Balance(poor) == 119 && await State(poor) == fundedBeforeRetry,
            "funding after rejection cannot execute a delayed original purchase");
        var unfunded = await f.Person(0); var unfundedOrder = f.Buy(unfunded);
        await NoChange(unfunded, unfundedOrder, "shop_insufficient_balance");
        if (dataSource is not null)
        {
            await using var connection = await dataSource.OpenConnectionAsync();
            await using var query = new NpgsqlCommand("SELECT count(*) FROM currency_balances WHERE owner_type='Character' AND owner_id=@owner AND currency_code='ShopGold'", connection);
            query.Parameters.AddWithValue("owner", unfunded.CharacterId.ToString("D"));
            assert((long)(await query.ExecuteScalarAsync())! == 0,
                "durable PostgreSQL rejection rolls back speculative zero-wallet insertion");
        }
        await NoChange(buyer, f.Buy(buyer) with { ExpectedUnitPrice = 11 }, "shop_price_changed");
        await NoChange(buyer, f.Buy(buyer) with { PolicyTag = new string('0', 64) }, "shop_policy_changed");
        var rich = await f.Person(9999); var richJunk = await f.Seed(rich, Fixture.Junk.DefinitionName, 160);
        await NoChange(rich, f.Sell(rich, richJunk), "shop_wallet_limit");
        var blocked = await f.Person(); var locked = await f.Seed(blocked, Fixture.Junk.DefinitionName, 160, locked: true);
        await NoChange(blocked, f.Sell(blocked, locked), "shop_item_unavailable");
        var quest = await f.Seed(blocked, Fixture.Junk.DefinitionName, 161, tags: ["Item.Quest.Required"]);
        await NoChange(blocked, f.Sell(blocked, quest), "shop_sale_not_allowed");
        var expired = await f.Seed(blocked, Fixture.Junk.DefinitionName, 162, expiry: f.Now.AddMinutes(-1));
        await NoChange(blocked, f.Sell(blocked, expired), "shop_item_unavailable");
        var full = await f.Person(2000);
        foreach (int slot in Enumerable.Range(80, 80)) await f.Seed(full, Fixture.Potion.DefinitionName, slot, 100);
        await NoChange(full, f.Buy(full), "shop_inventory_full");

        var concurrent = await f.Person(); var same = f.Buy(concurrent); var start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var attempts = Enumerable.Range(0, 16).Select(_ => Task.Run(async () =>
        { await start.Task; return await f.Store.ExecuteAsync(same, f.Catalog, default); })).ToArray();
        start.SetResult(); var receipts = await Task.WhenAll(attempts);
        assert(receipts.Select(r => JsonSerializer.Serialize(r)).Distinct().Count() == 1 && await f.Balance(concurrent) == 980
            && (await f.Owned(concurrent)).Where(i => i.DefinitionName == Fixture.Potion.DefinitionName).Sum(i => i.State.Quantity) == 2,
            "16 concurrent identical requests produce one debit and grant");
        var concurrentPoor = await f.Person(1); var sameDenied = f.Buy(concurrentPoor);
        string beforeConcurrentDenials = await State(concurrentPoor);
        var denialStart = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var denials = Enumerable.Range(0, 16).Select(_ => Task.Run(async () =>
        {
            await denialStart.Task;
            try { await f.Store.ExecuteAsync(sameDenied, f.Catalog, default); }
            catch (ShopRejectedException e) { return e; }
            throw new InvalidOperationException("Expected concurrent terminal rejection.");
        })).ToArray();
        denialStart.SetResult(); var rejectedReceipts = await Task.WhenAll(denials);
        assert(rejectedReceipts.All(r => r.Command == sameDenied && r.Code == "shop_insufficient_balance" && r.RejectedAt != default)
            && rejectedReceipts.Select(r => r.RejectedAt).Distinct().Count() == 1 && await f.Balance(concurrentPoor) == 1
            && await State(concurrentPoor) == beforeConcurrentDenials,
            "16 concurrent rejected requests share one terminal decision without asset changes");

        var heldActor = await f.Person(); var heldItem = await f.Seed(heldActor, Fixture.Junk.DefinitionName, 160);
        var reservation = f.Reservation(heldActor, 990, heldItem);
        await f.Reservations.ReserveAsync(reservation, default);
        var heldSnapshot = await f.Store.ReadAsync(heldActor, f.Catalog, default);
        assert(heldSnapshot.AvailableBalance == 10 && heldSnapshot.ReservedItems.Contains(heldItem.ItemId), "snapshot subtracts existing trade reservations");
        await NoChange(heldActor, f.Buy(heldActor), "shop_insufficient_balance");
        await NoChange(heldActor, f.Sell(heldActor, heldItem), "shop_item_unavailable");
        await f.Reservations.ReleaseAsync(reservation, default);
        await f.Store.ExecuteAsync(f.Sell(heldActor, heldItem), f.Catalog, default);
        assert(await f.Balance(heldActor) == 1004, "released item becomes sellable through same store");

        var leased = await f.Person(); var beforeLease = f.Buy(leased);
        var committed = await f.Store.ExecuteAsync(beforeLease, f.Catalog, default);
        await f.Game.CreateDungeonSessionAsync(leased.AccountId, leased.CharacterId, "ShopFixture", "Normal", f.Now, f.Now.AddMinutes(5), default);
        await NoChange(leased, f.Buy(leased), "shop_requires_lobby");
        assert(JsonSerializer.Serialize(await f.Store.ExecuteAsync(beforeLease, disabled, default)) == JsonSerializer.Serialize(committed)
            && await f.Store.FindAsync(leased, beforeLease.RequestId, default) is not null, "committed order remains recoverable after entering dungeon");
        using var canceled = new CancellationTokenSource(); canceled.Cancel();
        string cancellationBefore = await State(buyer); var canceledOrder = f.Buy(buyer);
        try { await f.Store.ExecuteAsync(canceledOrder, f.Catalog, canceled.Token); assert(false, "canceled order rejected"); }
        catch (OperationCanceledException) { assert(true, "canceled order rejected"); }
        assert(cancellationBefore == await State(buyer) && await f.Store.FindAsync(buyer, canceledOrder.RequestId, default) is null,
            "canceled order leaves no mutation or receipt");

        for (int round = 0; round < 4; ++round)
        {
            var actor = await f.Person(); var item = await f.Seed(actor, Fixture.Junk.DefinitionName, 160);
            var intent = f.Sell(actor, item); var hold = f.Reservation(actor, item: item);
            var begin = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var selling = Task.Run(async () =>
            {
                await begin.Task;
                try { await f.Store.ExecuteAsync(intent, f.Catalog, default); return true; }
                catch (ShopRuleException e) when (e.Code == "shop_item_unavailable") { return false; }
            });
            var reserving = Task.Run(async () =>
            {
                await begin.Task;
                try { await f.Reservations.ReserveAsync(hold, default); return true; }
                catch (TradeRuleException e) when (e.Code == "item_revision_conflict") { return false; }
            });
            begin.SetResult(); await Task.WhenAll(selling, reserving);
            assert(selling.Result != reserving.Result && await f.Balance(actor) == (selling.Result ? 1004 : 1000)
                && (await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == (selling.Result ? 2 : 4),
                "sale and trade reservation have exactly one winner");
            var entrant = await f.Person(); var purchase = f.Buy(entrant);
            var open = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var buying = Task.Run(async () =>
            {
                await open.Task;
                try { await f.Store.ExecuteAsync(purchase, f.Catalog, default); return true; }
                catch (ShopRuleException e) when (e.Code is "shop_requires_lobby" or "shop_lease_busy") { return false; }
            });
            var entering = Task.Run(async () =>
            {
                await open.Task;
                return await f.Game.CreateDungeonSessionAsync(entrant.AccountId, entrant.CharacterId, "ShopFixture", "Normal", f.Now, f.Now.AddMinutes(5), default);
            });
            open.SetResult(); await Task.WhenAll(buying, entering);
            assert(await f.Balance(entrant) == (buying.Result ? 980 : 1000)
                && (await f.Owned(entrant)).Where(i => i.DefinitionName == Fixture.Potion.DefinitionName).Sum(i => i.State.Quantity) == (buying.Result ? 2 : 0),
                "entry race serializes either whole purchase or whole denial");
            await NoChange(entrant, f.Buy(entrant), "shop_requires_lobby");
        }
    }
}
