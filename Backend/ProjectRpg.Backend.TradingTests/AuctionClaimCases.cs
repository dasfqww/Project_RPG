using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class AuctionClaimCases
{
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, true, 100)]);
    private static int checks, sequence;
    public static async Task<int> Run(bool restart)
    {
        try
        {
            var cs = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_AUCTION_CLAIM_TEST_POSTGRES"));
            if (cs.Host != "127.0.0.1" || cs.Database != "auction_claim_test"
                || !Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_RUN"), "N", out _)) throw new Exception("Owned private DB required");
            await using var data = NpgsqlDataSource.Create(cs.ConnectionString); var f = new Fixture(data);
            if (restart) await Reopen(f);
            else
            {
                var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog.Take(13).ToArray(), default) == 13, "v13 baseline");
                var old = await f.Person(); var item = await f.Seed(old);
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog, default) == 1, "v13 to v14 only");
                Check((await f.Items.FindAsync(item.ItemId, default))!.Revision == 1, "migration preserves inventory");
                await f.Define("Gold", CurrencyScope.Account, 1000);
                await PartialAndReplay(f); await Guards(f); await Rollback(f); await Access(f); await Races(f); await Scopes(f);
            }
            Console.WriteLine($"PASS Auction claim {(restart ? "separate-process restart" : "PostgreSQL")}: {checks} assertions. PG payable/wallet/receipt only; no auction API/UE validation.");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }

    private static async Task PartialAndReplay(Fixture f)
    {
        var seller = await f.Person(); await f.Fund(seller, 900); var sale = await f.Sale(seller);
        var p = (await f.Claims.FindPayableAsync(sale.Purchase.Listing.State.ListingId, seller, default))!;
        var cmd = Claim(p, seller, 400);
        var results = await Task.WhenAll(f.Claims.ClaimAsync(cmd, default), f.Claims.ClaimAsync(cmd, default)); var result = results[0];
        Check(Json(result) == Json(results[1]) && result.Paid == 100 && result.Payable.RemainingAmount == 565
            && result.Payable.Revision == 2 && await f.Balance(seller) == 1000, "parallel same request pays exactly wallet room once");
        var zeroCmd = Claim(result.Payable, seller, long.MaxValue); var zero = await f.Claims.ClaimAsync(zeroCmd, default);
        Check(zero.Paid == 0 && zero.Payable == result.Payable && zero.Payment.PreviousRevision == (await f.Wallet(seller)).Revision,
            "full wallet keeps payable and both revisions; durable zero receipt");
        await f.Fund(seller, -200);
        Check(Json(zero) == Json(await f.Claims.ClaimAsync(zeroCmd, default)) && await f.Balance(seller) == 800, "zero receipt replays without paying newly available room");
        var next = await f.Claims.ClaimAsync(Claim(zero.Payable, seller, long.MaxValue), default);
        Check(next.Paid == 200 && next.Payable.RemainingAmount == 365 && await f.Balance(seller) == 1000, "new request can claim after spending");
        await f.Fund(seller, -1000);
        var last = await f.Claims.ClaimAsync(Claim(next.Payable, seller, long.MaxValue), default);
        Check(last.Paid == 365 && last.Payable.RemainingAmount == 0 && await f.Balance(seller) == 365, "remaining proceeds paid exactly, original net conserved");
        var empty = await f.Claims.ClaimAsync(Claim(last.Payable, seller, 1), default);
        Check(empty.Paid == 0 && empty.Payable == last.Payable && empty.Payment.PreviousRevision == (await f.Wallet(seller)).Revision, "exhausted payable is a zero receipt with no revision churn");
        await Reject(() => f.Claims.ClaimAsync(cmd with { RequestedAmount = 401 }, default), "auction_idempotency_conflict");
        await Reject(() => f.Claims.ClaimAsync(cmd with { Claimant = seller with { CharacterId = Guid.NewGuid() } }, default), "auction_idempotency_conflict");
        await Reject(() => f.Claims.ClaimAsync(cmd with { RequestId = sale.Register.RequestId }, default), "auction_idempotency_conflict");
        await Reject(() => f.Claims.ClaimAsync(cmd with { RequestId = sale.Buy.RequestId }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.PurchaseAsync(sale.Buy with { RequestId = cmd.RequestId }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.ExecuteAsync(new(cmd.RequestId, cmd.ListingId, seller, AuctionEscrowKind.Cancel, 2), default), "auction_idempotency_conflict");
        Check(Json(sale.Purchase) == Json(await f.Store.PurchaseAsync(sale.Buy, default)), "purchase receipt unchanged after all seller claims");
        await f.Sql("CREATE TABLE auction_claim_restart_fixture(command jsonb,result jsonb,balance bigint); INSERT INTO auction_claim_restart_fixture VALUES(@c::jsonb,@r::jsonb,365)", ("c", Json(cmd)), ("r", Json(result)));
    }

    private static async Task Guards(Fixture f)
    {
        var seller = await f.Person(); var foreign = await f.Person(); var sale = await f.Sale(seller);
        var p = (await f.Claims.FindPayableAsync(sale.Buy.ListingId, seller, default))!; var cmd = Claim(p, seller, 100);
        Check(await f.Claims.FindPayableAsync(p.ListingId, foreign, default) is null, "foreign payable is hidden");
        await Reject(() => f.Claims.ClaimAsync(cmd with { Claimant = foreign }, default), "auction_payable_owner_required");
        await Reject(() => f.Claims.ClaimAsync(cmd with { Claimant = seller with { CharacterId = foreign.CharacterId } }, default), "character_not_owned");
        await Reject(() => f.Claims.ClaimAsync(cmd with { ExpectedRevision = 2 }, default), "auction_payable_revision_conflict");
        await Reject(() => f.Claims.ClaimAsync(cmd with { ExpectedCurrencyCode = "Other" }, default), "auction_currency_conflict");
        foreach (var invalid in new[] { cmd with { RequestId = Guid.Empty }, cmd with { RequestedAmount = 0 }, cmd with { RequestedAmount = -1 }, cmd with { ExpectedRevision = long.MaxValue }, cmd with { ExpectedCurrencyCode = "Gold\n" } })
            await Reject(() => f.Claims.ClaimAsync(invalid, default), "invalid_auction_claim");
        await Reject(() => f.Claims.ClaimAsync(cmd with { ListingId = Guid.NewGuid() }, default), "auction_payable_not_found");
        await f.Define("Gold", CurrencyScope.Account, 1000, false);
        await Reject(() => f.Claims.ClaimAsync(cmd, default), "auction_currency_unavailable");
        await f.Define("Gold", CurrencyScope.Account, 1000);
        // A reserved balance remains reserved while proceeds increase the same wallet.
        await f.Fund(seller, 200);
        var hold = new TradeReservationCommand(Guid.NewGuid(), Guid.NewGuid(), 1, seller, "Gold", new([], 150), DateTimeOffset.UtcNow.AddMinutes(5));
        await new PostgresTradeReservationRepository(f.Data, Policy).ReserveAsync(hold, default);
        var result = await f.Claims.ClaimAsync(Claim(p, seller, long.MaxValue), default);
        Check(result.Paid == 665 && await f.Balance(seller) == 865 && (await f.Change(seller, -716)).Status == CurrencyTransactionStatus.InsufficientBalance,
            "claim credits available room without releasing or spending direct-trade reservations");
        await f.Sql("UPDATE auction_seller_payables SET initial_amount=initial_amount+1 WHERE listing_id=@id", ("id", p.ListingId));
        await Expect<InvalidDataException>(() => f.Claims.FindPayableAsync(p.ListingId, seller, default));
        await f.Sql("UPDATE auction_seller_payables SET initial_amount=initial_amount-1 WHERE listing_id=@id", ("id", p.ListingId));
    }

    private static async Task Rollback(Fixture f)
    {
        foreach (var step in new[] { (Table: "auction_seller_payables", Event: "UPDATE"), (Table: "currency_balances", Event: "UPDATE"), (Table: "auction_claim_receipts", Event: "INSERT") })
        {
            var seller = await f.Person(); var sale = await f.Sale(seller); var p = (await f.Claims.FindPayableAsync(sale.Buy.ListingId, seller, default))!; var cmd = Claim(p, seller, 150);
            await f.Sql($"CREATE FUNCTION claim_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected'; END $$; CREATE TRIGGER claim_test_fail BEFORE {step.Event} ON {step.Table} FOR EACH ROW EXECUTE FUNCTION claim_test_fail()");
            try { await f.Claims.ClaimAsync(cmd, default); throw new Exception("missing injected failure"); }
            catch (PostgresException e) when (e.SqlState == "P0001") { checks++; }
            finally { await f.Sql($"DROP TRIGGER claim_test_fail ON {step.Table}; DROP FUNCTION claim_test_fail()"); }
            Check((await f.Claims.FindPayableAsync(p.ListingId, seller, default)) == p && await f.Balance(seller) == 0
                && await f.Scalar("SELECT count(*) FROM auction_claim_receipts WHERE request_id=@id", ("id", cmd.RequestId)) == 0, "failure at " + step.Table + " rolls back payable, wallet and receipt");
            var result = await f.Claims.ClaimAsync(cmd, default);
            Check(result.Paid == 150 && result.Payable.RemainingAmount == 515 && await f.Balance(seller) == 150, "failed request can retry after full rollback");
        }
    }

    private static async Task Access(Fixture f)
    {
        var seller = await f.Person(); var sale = await f.Sale(seller); var p = (await f.Claims.FindPayableAsync(sale.Buy.ListingId, seller, default))!; var cmd = Claim(p, seller, 150);
        await f.Game.CreateDungeonSessionAsync(seller.AccountId, seller.CharacterId, "Test", "Normal", DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5), default);
        await Reject(() => f.Claims.ClaimAsync(cmd, default), "auction_requires_lobby");
        await f.Sql("DELETE FROM character_session_leases WHERE character_id=@id", ("id", seller.CharacterId));
        await f.ServiceMode("Drain");
        try { await Expect<ServiceDrainingException>(() => f.Claims.ClaimAsync(cmd, default)); }
        finally { await f.ServiceMode("Open"); }
        await f.Restrict(seller);
        await Expect<AccountRestrictedException>(() => f.Claims.ClaimAsync(cmd, default));
        await f.Sql("DELETE FROM ops_account_restrictions WHERE account_id=@a", ("a", seller.AccountId));
        var result = await f.Claims.ClaimAsync(cmd, default); await f.Restrict(seller); await f.ServiceMode("Drain");
        try { Check(Json(result) == Json(await f.Claims.ClaimAsync(cmd, default)) && await f.Balance(seller) == 150, "committed claim replays after restriction/Drain without a second payment"); }
        finally { await f.ServiceMode("Open"); }
    }

    private static async Task Races(Fixture f)
    {
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            var seller = await f.Person(); var sale = await f.Sale(seller); var p = (await f.Claims.FindPayableAsync(sale.Buy.ListingId, seller, default))!;
            var a = Claim(p, seller, 500); var b = a with { RequestId = Guid.NewGuid() };
            var wins = await Task.WhenAll(TryClaim(f, a), TryClaim(f, b));
            Check(wins.Count(x => x is not null) == 1 && await f.Balance(seller) == 500
                && (await f.Claims.FindPayableAsync(p.ListingId, seller, default))!.RemainingAmount == 165, "different requests on one payable have one revision winner");
            var shared = await f.Person(); await f.Fund(shared, 900); var l1 = await f.Sale(shared); var l2 = await f.Sale(shared);
            var p1 = (await f.Claims.FindPayableAsync(l1.Buy.ListingId, shared, default))!; var p2 = (await f.Claims.FindPayableAsync(l2.Buy.ListingId, shared, default))!;
            var credits = await Task.WhenAll(f.Claims.ClaimAsync(Claim(p1, shared, long.MaxValue), default), f.Claims.ClaimAsync(Claim(p2, shared, long.MaxValue), default));
            Check(credits.Sum(x => x.Paid) == 100 && credits.Sum(x => x.Payable.RemainingAmount) == 1230 && await f.Balance(shared) == 1000, "different payables share wallet cap without losing proceeds");
            var s3 = await f.Person(); var l3 = await f.Sale(s3); var p3 = (await f.Claims.FindPayableAsync(l3.Buy.ListingId, s3, default))!;
            var claim = f.Claims.ClaimAsync(Claim(p3, s3, 500), default); var credit = f.Change(s3, 600); await Task.WhenAll(claim, credit);
            Check(await f.Balance(s3) == claim.Result.Paid + (credit.Result.Status == CurrencyTransactionStatus.Committed ? 600 : 0)
                && await f.Balance(s3) <= 1000 && claim.Result.Payable.RemainingAmount + claim.Result.Paid == 665, "normal currency credit vs claim respects cap and conservation");
        }
    }

    private static async Task Scopes(Fixture f)
    {
        foreach (var scope in Enum.GetValues<CurrencyScope>())
        {
            var code = "Claim" + scope; await f.Define(code, scope, long.MaxValue);
            var seller = await f.Person(); var alt = await f.Alt(seller); var sale = await f.Sale(seller, code, long.MaxValue);
            var p = (await f.Claims.FindPayableAsync(sale.Buy.ListingId, seller, default))!;
            if (scope == CurrencyScope.Character)
            {
                Check(await f.Claims.FindPayableAsync(p.ListingId, alt, default) is null, "same account alt cannot read character-bound payable");
                await Reject(() => f.Claims.ClaimAsync(Claim(p, alt, long.MaxValue), default), "auction_payable_owner_required");
            }
            else Check(await f.Claims.FindPayableAsync(p.ListingId, alt, default) == p, "same account/shared wallet alt may read payable");
            var actor = scope == CurrencyScope.Character ? seller : alt;
            var result = await f.Claims.ClaimAsync(Claim(p, actor, long.MaxValue), default);
            Check(result.Paid == p.InitialAmount && result.Payable.RemainingAmount == 0 && await f.Balance(actor, code) == p.InitialAmount
                && result.Payment.Currency.Scope == scope, "maximum int64 sale proceeds credited to trusted " + scope + " wallet");
            if (scope == CurrencyScope.Roster)
            {
                var roster = Guid.NewGuid();
                await f.Sql("INSERT INTO rosters(roster_id,steam_id,world_id,created_at,updated_at) VALUES(@id,@a,'claim-other',clock_timestamp(),clock_timestamp()); UPDATE characters SET roster_id=@id WHERE character_id=@char", ("id", roster), ("a", seller.AccountId), ("char", alt.CharacterId));
                Check(await f.Claims.FindPayableAsync(p.ListingId, alt, default) is null, "same account different roster cannot read old roster payable");
                await Reject(() => f.Claims.ClaimAsync(Claim(result.Payable, alt, 1), default), "auction_payable_owner_required");
            }
        }
    }

    private static async Task Reopen(Fixture f)
    {
        await using var c = await f.Data.OpenConnectionAsync(); await using var q = new NpgsqlCommand("SELECT command::text,result::text,balance FROM auction_claim_restart_fixture", c);
        AuctionClaimCommand cmd; AuctionClaimResult expected; long balance;
        await using (var r = await q.ExecuteReaderAsync()) { Check(await r.ReadAsync(), "previous process fixture present"); cmd = JsonSerializer.Deserialize<AuctionClaimCommand>(r.GetString(0))!; expected = JsonSerializer.Deserialize<AuctionClaimResult>(r.GetString(1))!; balance = r.GetInt64(2); }
        Check(Json(expected) == Json(await f.Claims.ClaimAsync(cmd, default)) && await f.Balance(cmd.Claimant) == balance
            && (await f.Claims.FindPayableAsync(cmd.ListingId, cmd.Claimant, default))!.RemainingAmount == 0, "separate process replays original partial receipt after payable exhausted without paying again");
    }
    private static AuctionClaimCommand Claim(AuctionSellerPayable p, TradeParticipant actor, long amount) => new(Guid.NewGuid(), p.ListingId, actor, p.Revision, p.CurrencyCode, amount);
    private static string Json<T>(T value) => JsonSerializer.Serialize(value);
    private static void Check(bool ok, string text) { if (!ok) throw new Exception(text); ++checks; }
    private static async Task Reject(Func<Task> action, string code) { try { await action(); } catch (TradeRuleException e) when (e.Code == code) { ++checks; return; } throw new Exception("Expected " + code); }
    private static async Task Expect<T>(Func<Task> action) where T : Exception { try { await action(); } catch (T) { ++checks; return; } throw new Exception("Expected " + typeof(T).Name); }
    private static async Task<AuctionClaimResult?> TryClaim(Fixture f, AuctionClaimCommand cmd) { try { return await f.Claims.ClaimAsync(cmd, default); } catch (TradeRuleException e) when (e.Code == "auction_payable_revision_conflict") { return null; } }

    private sealed class Fixture(NpgsqlDataSource data)
    {
        public NpgsqlDataSource Data => data;
        public PostgresAuctionClaimRepository Claims { get; } = new(data);
        public PostgresAuctionEscrowRepository Store { get; } = new(data, Policy, new(10, 500));
        public PostgresGameRepository Game { get; } = new(data, NullLogger<PostgresGameRepository>.Instance);
        public PostgresItemRepository Items { get; } = new(data, NullLogger<PostgresItemRepository>.Instance);
        public PostgresEconomyRepository Economy { get; } = new(data);
        public Task Define(string code, CurrencyScope scope, long max, bool enabled = true) => Economy.UpsertDefinitionAsync(new(code, code, scope, max, enabled), DateTimeOffset.UtcNow, default);
        public async Task<TradeParticipant> Person()
        { string account = (76561198690000000L + Interlocked.Increment(ref sequence)).ToString(); await Game.UpsertAccountAsync(new(account, account, false, false), DateTimeOffset.UtcNow, default); return await Alt(new(account, Guid.Empty)); }
        public async Task<TradeParticipant> Alt(TradeParticipant p)
        { var c = await Game.CreateCharacterAsync(p.AccountId, "Claim" + Interlocked.Increment(ref sequence), DateTimeOffset.UtcNow, default); return new(p.AccountId, c.CharacterId); }
        public async Task<ItemRecord> Seed(TradeParticipant p)
        {
            var owner = new ItemOwnerRef(ItemOwnerType.Character, p.CharacterId.ToString("D"));
            // Free the material slot on each successful sale by listing the entire stack.
            var item = new ItemRecord("Material", "Ore", 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, 160), new(Guid.NewGuid(), 1, 10, [], []), 0,
                ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, "Test", false));
            var result = await Items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "seed", owner, 10, [new(0, item)]), DateTimeOffset.UtcNow, default);
            Check(result.Status == ItemRepositoryCommitStatus.Committed, "seed fixture"); return result.Records.Single();
        }
        public async Task<(AuctionEscrowCommand Register, AuctionPurchaseCommand Buy, AuctionPurchaseResult Purchase)> Sale(TradeParticipant seller, string code = "Gold", long price = 699)
        {
            var buyer = await Person(); await Fund(buyer, price, code); var item = await Seed(seller);
            var cmd = new AuctionEscrowCommand(Guid.NewGuid(), Guid.NewGuid(), seller, AuctionEscrowKind.Register, Item: new(item.ItemId, item.Revision, 10), TotalPrice: price,
                ExpiresAt: DateTimeOffset.UtcNow.AddHours(1), Terms: new(code, 1, long.MaxValue, TimeSpan.FromMilliseconds(1), TimeSpan.FromDays(7), 500));
            var l = (await Store.ExecuteAsync(cmd, default)).Listing;
            var buy = new AuctionPurchaseCommand(Guid.NewGuid(), cmd.ListingId, buyer, 1, code, price);
            return (cmd, buy, await Store.PurchaseAsync(buy, default));
        }
        public async Task<CurrencyTransactionResult> Change(TradeParticipant p, long amount, string code = "Gold") => await Economy.CommitAsync(new(Guid.NewGuid(), p.CharacterId, Guid.NewGuid(), "Test.Fund", Guid.NewGuid().ToString("N"), "test", [new(code, amount)]), (await Game.GetCharacterEconomyContextAsync(p.CharacterId, default))!, DateTimeOffset.UtcNow, default);
        public async Task Fund(TradeParticipant p, long amount, string code = "Gold") => Check((await Change(p, amount, code)).Status == CurrencyTransactionStatus.Committed, "fund fixture");
        public async Task<CurrencyBalance> Wallet(TradeParticipant p, string code = "Gold") => (await Economy.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(p.CharacterId, default))!, default)).Balances.Single(x => x.Definition.CurrencyCode == code);
        public async Task<long> Balance(TradeParticipant p, string code = "Gold") => (await Wallet(p, code)).Balance;
        public async Task<long> Scalar(string text, params (string Name, object Value)[] values)
        { await using var c = await data.OpenConnectionAsync(); await using var q = new NpgsqlCommand(text, c); foreach (var v in values) q.Parameters.AddWithValue(v.Name, v.Value); return Convert.ToInt64(await q.ExecuteScalarAsync()); }
        public async Task Sql(string text, params (string Name, object Value)[] values)
        { await using var c = await data.OpenConnectionAsync(); await using var q = new NpgsqlCommand(text, c); foreach (var v in values) { if (v.Value is DBNull) q.Parameters.AddWithValue(v.Name, NpgsqlDbType.Varchar, DBNull.Value); else q.Parameters.AddWithValue(v.Name, v.Value); } await q.ExecuteNonQueryAsync(); }
        public async Task ServiceMode(string mode)
        { var id = await Audit("Service"); await Sql("UPDATE ops_service_control SET mode=@mode,revision=revision+1,updated_at=clock_timestamp(),action_request_id=@id WHERE id=1", ("mode", mode), ("id", id)); }
        public async Task Restrict(TradeParticipant p)
        { var id = await Audit("Account", p.AccountId); await Sql("INSERT INTO ops_account_restrictions VALUES(@account,1,'Active',clock_timestamp(),clock_timestamp()+interval '1 hour','test controlled fixture',@id)", ("account", p.AccountId), ("id", id)); }
        private async Task<Guid> Audit(string scope, string? account = null)
        { var id = Guid.NewGuid(); await Sql("INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,target_scope,target_account_id,reason,committed_at,result_json,origin_http_request_id) VALUES(@id,'Test',decode(repeat('00',32),'hex'),'{}','test',@scope,@account,'test controlled fixture',clock_timestamp(),'{}',@id)", ("id", id), ("scope", scope), ("account", (object?)account ?? DBNull.Value)); return id; }
    }
}
