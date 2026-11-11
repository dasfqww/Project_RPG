using System.Numerics;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class AuctionPurchaseCases
{
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, true, 100)]);
    private static readonly AuctionTerms Terms = new("Gold", 1, long.MaxValue, TimeSpan.FromMilliseconds(1), TimeSpan.FromDays(7), 500);
    private static int checks, sequence;
    public static async Task<int> Run(bool restart)
    {
        try
        {
            var cs = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES"));
            if (cs.Host != "127.0.0.1" || cs.Database != "equipment_test"
                || !Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_RUN_ID"), "N", out _))
                throw new Exception("Owned private DB required");
            await using var data = NpgsqlDataSource.Create(cs.ConnectionString);
            var f = new Fixture(data);
            if (restart) await Reopen(f);
            else
            {
                var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog.Take(12).ToArray(), default) == 12, "v12 baseline");
                var old = await f.Person(); var item = await f.Seed(old);
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog.Take(13).ToArray(), default) == 1, "v12 to v13 only");
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog, default) == catalog.Count - 13, "claim schema dependency");
                Check((await f.Items.FindAsync(item.ItemId, default))!.Revision == 1, "v13 preserves old inventory");
                await f.Define("Gold", CurrencyScope.Account, 1000);
                await SuccessAndReplay(f); await Delivery(f); await Guards(f); await Rollback(f); await Access(f); await Races(f); await CurrencyScopes(f);
            }
            Console.WriteLine($"PASS Auction purchase {(restart ? "separate-process restart" : "PostgreSQL")}: {checks} assertions. Atomic purchase/payable only; no claim/API/UE validation.");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }

    private static async Task SuccessAndReplay(Fixture f)
    {
        var seller = await f.Person(); var buyer = await f.Person(); await f.Fund(seller, 1000); await f.Fund(buyer, 1000);
        var source = await f.Seed(seller); var registration = Register(seller, source);
        var held = (await f.Store.ExecuteAsync(registration, default)).Listing;
        var cmd = Buy(held, buyer);
        var duplicates = await Task.WhenAll(f.Store.PurchaseAsync(cmd, default), f.Store.PurchaseAsync(cmd, default));
        var result = duplicates[0]; var sale = result.Listing.Sale!;
        Check(Json(result) == Json(duplicates[1]), "same request parallel replay is exact");
        Check(result.Listing.State.Status == AuctionListingStatus.SaleAgreed && result.Listing.State.Revision == 2
            && sale.Amounts == new AuctionSaleAmounts(699, 665, 34), "stored sold proof uses integer fee and net");
        Check(result.Payment.PreviousBalance == 1000 && result.Payment.NewBalance == 301 && await f.Balance(buyer) == 301
            && await f.Balance(seller) == 1000, "buyer debited once; full/offline seller wallet unchanged");
        Check(await f.Amount(held.State.ListingId) == 665 && await f.Scalar("SELECT count(*) FROM auction_sales WHERE listing_id=@id", ("id", held.State.ListingId)) == 1,
            "one immutable sale and seller payable");
        var delivered = (await f.Items.FindAsync(held.EscrowAtRegistration.ItemId, default))!;
        Check(delivered.Owner.OwnerId == buyer.CharacterId.ToString("D") && delivered.Location.ContainerType == ItemContainerType.Inventory
            && delivered.State.Quantity == 4 && delivered.State.GenerationSeed == source.State.GenerationSeed
            && delivered.Metadata == source.Metadata && delivered.State.StatValues.SequenceEqual(source.State.StatValues), "split identity and options arrive at buyer");
        Check((await f.Items.FindAsync(source.ItemId, default))!.State.Quantity == 6, "seller retains only unlisted remainder");
        Check(Json(result.Listing) == Json(await f.Store.FindAsync(held.State.ListingId, seller, default)), "seller read contains committed sale proof");
        Check((await f.Store.ExecuteAsync(registration, default)).Listing.Sale is null,
            "original registration receipt stays unsold snapshot after completed purchase");
        await Reject(() => f.Store.PurchaseAsync(cmd with { ExpectedTotalPrice = 700 }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.PurchaseAsync(cmd with { Buyer = buyer with { CharacterId = Guid.NewGuid() } }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.ExecuteAsync(Close(held) with { RequestId = cmd.RequestId }, default), "auction_idempotency_conflict");
        var other = (await f.Store.ExecuteAsync(Register(seller, (await f.Items.FindAsync(source.ItemId, default))!, 1), default)).Listing;
        await Reject(() => f.Store.PurchaseAsync(Buy(other, buyer) with { RequestId = registration.RequestId }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.PurchaseAsync(cmd with { ListingId = other.State.ListingId }, default), "auction_idempotency_conflict");
        // Move the purchased identity into a different listing before a new process replays the old receipt.
        await f.Store.ExecuteAsync(Register(buyer, delivered, 4), default);
        await f.Fund(buyer, 1);
        await f.Sql("CREATE TABLE auction_purchase_restart_fixture(command jsonb,result jsonb,balance bigint)");
        await f.Sql("INSERT INTO auction_purchase_restart_fixture VALUES(@c::jsonb,@r::jsonb,302)", ("c", Json(cmd)), ("r", Json(result)));
        Check(Json(result) == Json(await f.Store.PurchaseAsync(cmd, default)) && await f.Balance(buyer) == 302, "replay after buyer relists item does not deliver/debit again");
    }

    private static async Task Delivery(Fixture f)
    {
        var seller = await f.Person(); var buyer = await f.Person(); await f.Fund(buyer, 1000);
        var source = await f.Seed(seller); var merge = await f.Seed(buyer, 98);
        var held = (await f.Store.ExecuteAsync(Register(seller, source, 10), default)).Listing;
        var result = await f.Store.PurchaseAsync(Buy(held, buyer), default);
        Check((await f.Items.FindAsync(merge.ItemId, default))!.State.Quantity == 100
            && (await f.Items.FindAsync(source.ItemId, default))!.State.Quantity == 8
            && result.Items.Count == 2, "whole identity remainder survives partial target merge");
        var s2 = await f.Person(); var b2 = await f.Person(); await f.Fund(b2, 1000);
        var i2 = await f.Seed(s2); var target = await f.Seed(b2, 96);
        var l2 = (await f.Store.ExecuteAsync(Register(s2, i2), default)).Listing;
        await f.Store.PurchaseAsync(Buy(l2, b2), default);
        Check((await f.Items.FindAsync(target.ItemId, default))!.State.Quantity == 100
            && (await f.Items.FindAsync(l2.EscrowAtRegistration.ItemId, default))!.LifecycleState == ItemLifecycleState.Consumed, "full merge tombstones vault identity");
        var s3 = await f.Person(); var b3 = await f.Person(); await f.Fund(b3, 1000);
        var i3 = await f.Seed(s3);
        for (int slot = 160; slot < 240; ++slot) await f.Seed(b3, 100, slot, slot);
        var l3 = (await f.Store.ExecuteAsync(Register(s3, i3, 10), default)).Listing;
        var mail = await f.Store.PurchaseAsync(Buy(l3, b3), default);
        Check(mail.Items.Single().ItemId == i3.ItemId && mail.Items.Single().Location.ContainerType == ItemContainerType.Mail, "full buyer material bag delivers stable identity to mail");
        var s4 = await f.Person(); var b4 = await f.Person(); await f.Fund(b4, 1000);
        var i4 = await f.Seed(s4); var l4 = (await f.Store.ExecuteAsync(Register(s4, i4), default)).Listing;
        var tiny = new PostgresAuctionEscrowRepository(f.Data, Policy, new(10, 3));
        await Reject(() => tiny.PurchaseAsync(Buy(l4, b4), default), "auction_delivery_capacity");
        await f.Unchanged(l4, b4, 1000);
        var s5 = await f.Person(); var b5 = await f.Person(); await f.Fund(b5, 1000);
        var i5 = await f.Seed(s5); var capped = await f.Seed(b5, 49);
        var l5 = (await f.Store.ExecuteAsync(Register(s5, i5), default)).Listing;
        var reduced = new PostgresAuctionEscrowRepository(f.Data, new([new("Material", "Ore", 1, true, true, 50)]), new(10, 500));
        await reduced.PurchaseAsync(Buy(l5, b5), default);
        Check((await f.Items.FindAsync(capped.ItemId, default))!.State.Quantity == 50
            && (await f.Items.FindAsync(l5.EscrowAtRegistration.ItemId, default))!.State.Quantity == 3, "live reduced cap governs merge while registration proof remains unchanged");
        var s6 = await f.Person(); var b6 = await f.Person(); await f.Fund(b6, 1000);
        var reservedTarget = await f.Seed(b6, 90); var l6 = (await f.Store.ExecuteAsync(Register(s6, await f.Seed(s6)), default)).Listing;
        var hold = new TradeReservationCommand(Guid.NewGuid(), Guid.NewGuid(), 1, b6, "Gold", new([new(reservedTarget.ItemId, reservedTarget.Revision, 1)], 0), DateTimeOffset.UtcNow.AddMinutes(5));
        await new PostgresTradeReservationRepository(f.Data, Policy).ReserveAsync(hold, default);
        await f.Store.PurchaseAsync(Buy(l6, b6), default);
        Check((await f.Items.FindAsync(reservedTarget.ItemId, default))!.Revision == 1
            && (await f.Items.FindAsync(l6.EscrowAtRegistration.ItemId, default))!.State.Quantity == 4, "buyer reserved stack skipped without mutation");
    }

    private static async Task Guards(Fixture f)
    {
        var seller = await f.Person(); var buyer = await f.Person(); await f.Fund(buyer, 1000);
        var l = (await f.Store.ExecuteAsync(Register(seller, await f.Seed(seller)), default)).Listing; var cmd = Buy(l, buyer);
        await Reject(() => f.Store.PurchaseAsync(cmd with { ExpectedRevision = 2 }, default), "auction_revision_conflict");
        await Reject(() => f.Store.PurchaseAsync(cmd with { ExpectedTotalPrice = 698 }, default), "auction_price_conflict");
        await Reject(() => f.Store.PurchaseAsync(cmd with { ExpectedCurrencyCode = "Silver" }, default), "auction_price_conflict");
        await Reject(() => f.Store.PurchaseAsync(cmd with { Buyer = seller }, default), "auction_self_purchase");
        var alt = await f.Alt(seller);
        await Reject(() => f.Store.PurchaseAsync(cmd with { Buyer = alt }, default), "auction_self_purchase");
        await Reject(() => f.Store.PurchaseAsync(cmd with { Buyer = buyer with { CharacterId = seller.CharacterId } }, default), "auction_self_purchase");
        var foreign = await f.Person();
        await Reject(() => f.Store.PurchaseAsync(cmd with { Buyer = buyer with { CharacterId = foreign.CharacterId } }, default), "character_not_owned");
        await Reject(() => f.Store.PurchaseAsync(cmd with { Buyer = foreign }, default), "auction_insufficient_balance");
        var buyerAlt = await f.Alt(buyer);
        var hold = new TradeReservationCommand(Guid.NewGuid(), Guid.NewGuid(), 1, buyerAlt, "Gold", new([], 400), DateTimeOffset.UtcNow.AddMinutes(5));
        var reservations = new PostgresTradeReservationRepository(f.Data, Policy); await reservations.ReserveAsync(hold, default);
        await Reject(() => f.Store.PurchaseAsync(cmd, default), "auction_insufficient_balance");
        await reservations.ReleaseAsync(hold, default);
        await Reject(() => new PostgresAuctionEscrowRepository(f.Data, new([]), new(10, 500)).PurchaseAsync(cmd, default), "item_trade_not_allowed");
        await f.Define("Gold", CurrencyScope.Account, 1000, false);
        await Reject(() => f.Store.PurchaseAsync(cmd, default), "auction_currency_unavailable");
        await f.Define("Gold", CurrencyScope.Account, 1000);
        await f.Sql("UPDATE item_records SET revision=revision+1 WHERE item_id=@id", ("id", l.EscrowAtRegistration.ItemId));
        await Reject(() => f.Store.PurchaseAsync(cmd, default), "auction_escrow_changed");
        Check(await f.Balance(buyer) == 1000 && await f.Amount(l.State.ListingId) == -1, "all guards leave wallet and payable untouched");
        var expSeller = await f.Person(); var expBuyer = await f.Person(); await f.Fund(expBuyer, 1000);
        var expCmd = Register(expSeller, await f.Seed(expSeller)) with { ExpiresAt = DateTimeOffset.UtcNow.AddSeconds(1) };
        var exp = (await f.Store.ExecuteAsync(expCmd, default)).Listing;
        await Task.Delay(1100);
        var purchase = TryRule(() => f.Store.PurchaseAsync(Buy(exp, expBuyer), default));
        var expiration = f.Store.ExecuteAsync(Close(exp) with { Kind = AuctionEscrowKind.Expire }, default);
        await Task.WhenAll(purchase, expiration);
        Check(!purchase.Result, "expired purchase vs expiration cannot sell");
        var expired = expiration.Result;
        Check(expired.Listing.State.Status == AuctionListingStatus.Expired && await f.Balance(expBuyer) == 1000
            && await f.Amount(exp.State.ListingId) == -1, "expiry returns unsold item without money movement");
    }

    private static async Task Rollback(Fixture f)
    {
        foreach (var table in new[] { "auction_sales", "auction_seller_payables" })
        {
            var seller = await f.Person(); var buyer = await f.Person(); await f.Fund(buyer, 1000);
            var target = await f.Seed(buyer, 98); var l = (await f.Store.ExecuteAsync(Register(seller, await f.Seed(seller)), default)).Listing;
            var cmd = Buy(l, buyer);
            await f.Sql($"CREATE FUNCTION purchase_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected'; END $$; CREATE TRIGGER purchase_test_fail BEFORE INSERT ON {table} FOR EACH ROW EXECUTE FUNCTION purchase_test_fail()");
            try { await f.Store.PurchaseAsync(cmd, default); throw new Exception("missing injected purchase failure"); }
            catch (PostgresException e) when (e.SqlState == "P0001") { checks++; }
            finally { await f.Sql($"DROP TRIGGER purchase_test_fail ON {table}; DROP FUNCTION purchase_test_fail()"); }
            await f.Unchanged(l, buyer, 1000);
            Check((await f.Items.FindAsync(target.ItemId, default))!.State.Quantity == 98
                && await f.Scalar("SELECT count(*) FROM auction_sales WHERE request_id=@id", ("id", cmd.RequestId)) == 0, "injected receipt/payable failure rolls back buyer merge and immutable sale");
            var result = await f.Store.PurchaseAsync(cmd, default);
            Check(result.Payment.NewBalance == 301 && await f.Amount(l.State.ListingId) == 665, "same failed request retries successfully after rollback");
        }
    }

    private static async Task Access(Fixture f)
    {
        var seller = await f.Person(); var buyer = await f.Person(); await f.Fund(buyer, 1000);
        var l = (await f.Store.ExecuteAsync(Register(seller, await f.Seed(seller)), default)).Listing; var cmd = Buy(l, buyer);
        await f.Game.CreateDungeonSessionAsync(buyer.AccountId, buyer.CharacterId, "Test", "Normal", DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5), default);
        await Reject(() => f.Store.PurchaseAsync(cmd, default), "auction_requires_lobby");
        await f.Sql("DELETE FROM character_session_leases WHERE character_id=@id", ("id", buyer.CharacterId));
        await f.ServiceMode("Drain");
        try { await Expect<ServiceDrainingException>(() => f.Store.PurchaseAsync(cmd, default)); }
        finally { await f.ServiceMode("Open"); }
        await f.Restrict(seller);
        await Expect<AccountRestrictedException>(() => f.Store.PurchaseAsync(cmd, default));
        await f.Sql("DELETE FROM ops_account_restrictions WHERE account_id=@a", ("a", seller.AccountId));
        await f.Restrict(buyer);
        await Expect<AccountRestrictedException>(() => f.Store.PurchaseAsync(cmd, default));
        await f.Sql("DELETE FROM ops_account_restrictions WHERE account_id=@a", ("a", buyer.AccountId));
        await f.Game.CreateDungeonSessionAsync(seller.AccountId, seller.CharacterId, "Test", "Normal", DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5), default);
        var result = await f.Store.PurchaseAsync(cmd, default);
        Check(result.Listing.Sale is not null, "seller active dungeon lease does not prevent offline-style sale");
        await f.Restrict(buyer); await f.ServiceMode("Drain");
        try { Check(Json(result) == Json(await f.Store.PurchaseAsync(cmd, default)), "committed receipt replay allowed after ban/drain"); }
        finally { await f.ServiceMode("Open"); }
    }

    private static async Task Races(Fixture f)
    {
        for (int iteration = 0; iteration < 4; ++iteration)
        {
            var seller = await f.Person(); var a = await f.Person(); var b = await f.Person(); await f.Fund(a, 1000); await f.Fund(b, 1000);
            var l = (await f.Store.ExecuteAsync(Register(seller, await f.Seed(seller)), default)).Listing;
            var buyers = await Task.WhenAll(TryRule(() => f.Store.PurchaseAsync(Buy(l, a), default)), TryRule(() => f.Store.PurchaseAsync(Buy(l, b), default)));
            Check(buyers.Count(x => x) == 1 && await f.Balance(a) + await f.Balance(b) == 1301
                && await f.Amount(l.State.ListingId) == 665, "two buyers have one winner/debit/payable");
            var s2 = await f.Person(); var b2 = await f.Person(); await f.Fund(b2, 1000);
            var i2 = await f.Seed(s2); var l2 = (await f.Store.ExecuteAsync(Register(s2, i2, 10), default)).Listing;
            var results = await Task.WhenAll(TryRule(() => f.Store.PurchaseAsync(Buy(l2, b2), default)), TryRule(() => f.Store.ExecuteAsync(Close(l2), default)));
            var final = (await f.Store.FindAsync(l2.State.ListingId, s2, default))!;
            Check(results.Count(x => x) == 1 && (final.Sale is not null
                ? await f.Balance(b2) == 301 && await f.Amount(l2.State.ListingId) == 665
                : await f.Balance(b2) == 1000 && await f.Amount(l2.State.ListingId) == -1), "purchase vs cancellation commits exactly one disposition");
            var s3 = await f.Person(); var b3 = await f.Person(); await f.Fund(b3, 800);
            var l3 = (await f.Store.ExecuteAsync(Register(s3, await f.Seed(s3)), default)).Listing;
            var purchase = TryRule(() => f.Store.PurchaseAsync(Buy(l3, b3), default)); var spend = f.Change(b3, -200);
            await Task.WhenAll(purchase, spend);
            Check((purchase.Result ? 1 : 0) + (spend.Result.Status == CurrencyTransactionStatus.Committed ? 1 : 0) == 1
                && await f.Balance(b3) == (purchase.Result ? 101 : 600), "normal spending vs purchase shares wallet lock without overspend");
        }
    }

    private static async Task CurrencyScopes(Fixture f)
    {
        foreach (var scope in Enum.GetValues<CurrencyScope>())
        {
            var code = "Scope" + scope; await f.Define(code, scope, long.MaxValue);
            var seller = await f.Person(); var buyer = await f.Person(); await f.Fund(buyer, long.MaxValue, code);
            var terms = new AuctionTerms(code, 1, long.MaxValue, TimeSpan.FromMilliseconds(1), TimeSpan.FromDays(7), 333);
            var l = (await f.Store.ExecuteAsync(Register(seller, await f.Seed(seller)) with { Terms = terms, TotalPrice = long.MaxValue }, default)).Listing;
            var result = await f.Store.PurchaseAsync(Buy(l, buyer), default); var sale = result.Listing.Sale!;
            long fee = (long)((BigInteger)long.MaxValue * 333 / 10000);
            Check(sale.Currency.Scope == scope && sale.Amounts.Fee == fee && sale.Amounts.SellerPayable == long.MaxValue - fee
                && result.Payment.NewBalance == 0 && await f.Amount(l.State.ListingId) == long.MaxValue - fee, "int64 maximum sale exact for " + scope);
            var context = (await f.Game.GetCharacterEconomyContextAsync(seller.CharacterId, default))!;
            Check(sale.SellerWallet == new CurrencyOwnerRef(scope, EconomyRules.ResolveOwnerId(scope, context))
                && await f.Scalar("SELECT count(*) FROM auction_seller_payables WHERE listing_id=@id AND wallet_owner_type=@type AND wallet_owner_id=@owner", ("id", l.State.ListingId), ("type", scope.ToString()), ("owner", sale.SellerWallet.OwnerId)) == 1,
                "seller payable scope resolved from trusted character context");
        }
    }

    private static async Task Reopen(Fixture f)
    {
        await using var c = await f.Data.OpenConnectionAsync(); await using var q = new NpgsqlCommand("SELECT command::text,result::text,balance FROM auction_purchase_restart_fixture", c);
        AuctionPurchaseCommand cmd; AuctionPurchaseResult expected; long balance;
        await using (var r = await q.ExecuteReaderAsync()) { Check(await r.ReadAsync(), "previous process fixture present"); cmd = JsonSerializer.Deserialize<AuctionPurchaseCommand>(r.GetString(0))!; expected = JsonSerializer.Deserialize<AuctionPurchaseResult>(r.GetString(1))!; balance = r.GetInt64(2); }
        var result = await f.Store.PurchaseAsync(cmd, default);
        Check(Json(result) == Json(expected) && await f.Balance(cmd.Buyer) == balance
            && await f.Amount(cmd.ListingId) == 665, "new process replays original immutable result after relisting without duplicate debit/payable");
    }
    private static AuctionEscrowCommand Register(TradeParticipant p, ItemRecord item, int quantity = 4) => new(Guid.NewGuid(), Guid.NewGuid(), p, AuctionEscrowKind.Register,
        Item: new(item.ItemId, item.Revision, quantity), TotalPrice: 699, ExpiresAt: DateTimeOffset.UtcNow.AddHours(1), Terms: Terms);
    private static AuctionPurchaseCommand Buy(AuctionEscrowListing l, TradeParticipant p) => new(Guid.NewGuid(), l.State.ListingId, p, l.State.Revision, l.State.Terms.CurrencyCode, l.State.TotalPrice);
    private static AuctionEscrowCommand Close(AuctionEscrowListing l) => new(Guid.NewGuid(), l.State.ListingId, l.State.Seller, AuctionEscrowKind.Cancel, l.State.Revision);
    private static string Json<T>(T value) => JsonSerializer.Serialize(value);
    private static void Check(bool ok, string text) { if (!ok) throw new Exception(text); ++checks; }
    private static async Task Reject(Func<Task> action, string code) { try { await action(); } catch (TradeRuleException e) when (e.Code == code) { ++checks; return; } throw new Exception("Expected " + code); }
    private static async Task Expect<T>(Func<Task> action) where T : Exception { try { await action(); } catch (T) { ++checks; return; } throw new Exception("Expected " + typeof(T).Name); }
    private static async Task<bool> TryRule(Func<Task> action) { try { await action(); return true; } catch (TradeRuleException e) when (e.Code is "auction_revision_conflict" or "auction_not_active" or "auction_insufficient_balance" or "auction_expired") { return false; } }
    private sealed class Fixture(NpgsqlDataSource data)
    {
        public NpgsqlDataSource Data => data;
        public PostgresAuctionEscrowRepository Store { get; } = new(data, Policy, new(10, 500));
        public PostgresGameRepository Game { get; } = new(data, NullLogger<PostgresGameRepository>.Instance);
        public PostgresItemRepository Items { get; } = new(data, NullLogger<PostgresItemRepository>.Instance);
        public PostgresEconomyRepository Economy { get; } = new(data);
        public Task Define(string code, CurrencyScope scope, long max, bool enabled = true) => Economy.UpsertDefinitionAsync(new(code, code, scope, max, enabled), DateTimeOffset.UtcNow, default);
        public async Task<TradeParticipant> Person()
        {
            string account = (76561198590000000L + Interlocked.Increment(ref sequence)).ToString();
            await Game.UpsertAccountAsync(new(account, account, false, false), DateTimeOffset.UtcNow, default); return await Alt(new(account, Guid.Empty));
        }
        public async Task<TradeParticipant> Alt(TradeParticipant p)
        { var c = await Game.CreateCharacterAsync(p.AccountId, "Purchase" + Interlocked.Increment(ref sequence), DateTimeOffset.UtcNow, default); return new(p.AccountId, c.CharacterId); }
        public async Task<ItemRecord> Seed(TradeParticipant p, int quantity = 10, int slot = 160, int seed = 1)
        {
            var owner = new ItemOwnerRef(ItemOwnerType.Character, p.CharacterId.ToString("D"));
            var item = new ItemRecord("Material", "Ore", 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, slot),
                new(Guid.NewGuid(), seed, quantity, ["Item.Ore"], [new("Stat.Value", 4)]), 0, ItemLifecycleState.Active,
                new(ItemBindState.Unbound, new(0, 0), null, "Test", false));
            var result = await Items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "seed", owner, quantity, [new(0, item)]), DateTimeOffset.UtcNow, default);
            Check(result.Status == ItemRepositoryCommitStatus.Committed, "seed fixture"); return result.Records.Single();
        }
        public async Task<CurrencyTransactionResult> Change(TradeParticipant p, long amount, string code = "Gold") => await Economy.CommitAsync(
            new(Guid.NewGuid(), p.CharacterId, Guid.NewGuid(), "Test.Fund", Guid.NewGuid().ToString("N"), "test", [new(code, amount)]),
            (await Game.GetCharacterEconomyContextAsync(p.CharacterId, default))!, DateTimeOffset.UtcNow, default);
        public async Task Fund(TradeParticipant p, long amount, string code = "Gold") => Check((await Change(p, amount, code)).Status == CurrencyTransactionStatus.Committed, "fund fixture");
        public async Task<long> Balance(TradeParticipant p) => (await Economy.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(p.CharacterId, default))!, default)).Balances.Single(x => x.Definition.CurrencyCode == "Gold").Balance;
        public async Task<long> Amount(Guid id) => await Scalar("SELECT coalesce((SELECT remaining_amount FROM auction_seller_payables WHERE listing_id=@id),-1)", ("id", id));
        public async Task Unchanged(AuctionEscrowListing l, TradeParticipant buyer, long balance)
        {
            Check((await Store.FindAsync(l.State.ListingId, l.State.Seller, default))!.State.Status == AuctionListingStatus.Active
                && Json(await Items.FindAsync(l.EscrowAtRegistration.ItemId, default)) == Json(l.EscrowAtRegistration)
                && await Balance(buyer) == balance && await Amount(l.State.ListingId) == -1, "failed purchase preserves active escrow/wallet/no payable");
        }
        public async Task<long> Scalar(string text, params (string Name, object Value)[] values)
        { await using var c = await data.OpenConnectionAsync(); await using var q = new NpgsqlCommand(text, c); foreach (var v in values) q.Parameters.AddWithValue(v.Name, v.Value); return Convert.ToInt64(await q.ExecuteScalarAsync()); }
        public async Task Sql(string text, params (string Name, object Value)[] values)
        { await using var c = await data.OpenConnectionAsync(); await using var q = new NpgsqlCommand(text, c); foreach (var v in values) { if (v.Value is DBNull) q.Parameters.AddWithValue(v.Name, NpgsqlDbType.Varchar, DBNull.Value); else q.Parameters.AddWithValue(v.Name, v.Value); } await q.ExecuteNonQueryAsync(); }
        public async Task ServiceMode(string mode)
        { var id = await Audit("Service"); await Sql("UPDATE ops_service_control SET mode=@mode,revision=revision+1,updated_at=clock_timestamp(),action_request_id=@id WHERE id=1", ("mode", mode), ("id", id)); }
        public async Task Restrict(TradeParticipant p)
        { var id = await Audit("Account", p.AccountId); await Sql("INSERT INTO ops_account_restrictions VALUES(@account,1,'Active',clock_timestamp(),clock_timestamp()+interval '1 hour','test controlled fixture',@id)", ("account", p.AccountId), ("id", id)); }
        private async Task<Guid> Audit(string scope, string? account = null)
        {
            var id = Guid.NewGuid(); await Sql("INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,target_scope,target_account_id,reason,committed_at,result_json,origin_http_request_id) VALUES(@id,'Test',decode(repeat('00',32),'hex'),'{}','test',@scope,@account,'test controlled fixture',clock_timestamp(),'{}',@id)", ("id", id), ("scope", scope), ("account", (object?)account ?? DBNull.Value)); return id;
        }
    }
}
