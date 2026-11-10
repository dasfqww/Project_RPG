using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class AuctionEscrowCases
{
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, true, 100)]);
    private static readonly AuctionTerms Terms = new("Gold", 1, long.MaxValue, TimeSpan.FromMilliseconds(1), TimeSpan.FromDays(7), 500);
    private static int checks, sequence;
    public static async Task<int> Run(bool restart)
    {
        try
        {
            var cs = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_POSTGRES"));
            if (cs.Host != "127.0.0.1" || cs.Database != "trade_test"
                || !Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_RUN"), "N", out _)) throw new Exception("Owned private DB required");
            await using var data = NpgsqlDataSource.Create(cs.ConnectionString);
            var f = new Fixture(data);
            if (restart) await Reopen(f);
            else
            {
                var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog.Take(11).ToArray(), default) == 11, "v11 baseline");
                var old = await f.Person(); var item = await f.Seed(old);
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog.Take(12).ToArray(), default) == 1, "v11 to v12 only");
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog, default) == catalog.Count - 12, "purchase schema dependency");
                Check((await f.Items.FindAsync(item.ItemId, default))!.Revision == 1, "migration preserves old inventory");
                await f.Economy.UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Account, long.MaxValue, true), DateTimeOffset.UtcNow, default);
                await FullAndReplay(f); await PartialAndGuards(f); await Rollback(f); await Returns(f); await Expiry(f); await AccessAndLimits(f); await Races(f);
            }
            Console.WriteLine($"PASS Auction escrow {(restart ? "separate-process restart" : "PostgreSQL")}: {checks} assertions. Registration/return only; no purchase or API/UE claim.");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }

    private static async Task FullAndReplay(Fixture f)
    {
        var seller = await f.Person(); var item = await f.Seed(seller);
        var command = Register(seller, item, 10);
        var result = await f.Store.ExecuteAsync(command, default);
        var held = (await f.Items.FindAsync(item.ItemId, default))!;
        Check(held.Owner == AuctionEscrowRules.Owner(command.ListingId) && held.Location.ContainerType == ItemContainerType.Auction
            && held.State.Quantity == 10 && held.Revision == 2 && result.Listing.Split is null, "whole escrow preserves ID/options and advances revision");
        Check(held.State.GenerationSeed == item.State.GenerationSeed && held.Metadata == item.Metadata && held.State.StatValues.SequenceEqual(item.State.StatValues), "metadata preserved");
        Check(await f.Store.FindAsync(command.ListingId, await f.Person(), default) is null, "foreign listing hidden");
        await Reject(() => f.Store.ExecuteAsync(Close(result.Listing) with { Seller = seller with { AccountId = "76561198000000009" } }, default), "auction_seller_required");
        var closed = await new PostgresAuctionEscrowRepository(f.Data, new TradeItemPolicy([]), new(10, 500)).ExecuteAsync(Close(result.Listing), default);
        var returned = (await f.Items.FindAsync(item.ItemId, default))!;
        Check(closed.Listing.State.Status == AuctionListingStatus.Cancelled && returned.Owner == item.Owner && returned.Location == item.Location && returned.Revision == 3, "cancel returns original ID to inventory despite removed allowlist");
        Check(JsonSerializer.Serialize(result) == JsonSerializer.Serialize(await f.Store.ExecuteAsync(command, default)), "old registration receipt replays after close without reserving again");
        Check(JsonSerializer.Serialize(closed) == JsonSerializer.Serialize(await f.Store.ExecuteAsync(Close(result.Listing) with { RequestId = closed.RequestId }, default)), "close receipt exact replay");
        await Reject(() => f.Store.ExecuteAsync(command with { TotalPrice = 700 }, default), "auction_idempotency_conflict");
        await f.Sql("CREATE TABLE auction_restart_fixture(command jsonb,result jsonb)");
        await f.Sql("INSERT INTO auction_restart_fixture VALUES(@c::jsonb,@r::jsonb)", ("c", JsonSerializer.Serialize(command)), ("r", JsonSerializer.Serialize(result)));
        var relisted = await f.Store.ExecuteAsync(Register(seller, returned, 10), default);
        Check(relisted.Listing.State.Status == AuctionListingStatus.Active, "same item can be listed again after close");
        await f.Store.ExecuteAsync(Close(relisted.Listing), default);
    }
    private static async Task PartialAndGuards(Fixture f)
    {
        var p = await f.Person(); var item = await f.Seed(p); var command = Register(p, item, 4);
        var r = await f.Store.ExecuteAsync(command, default);
        var source = (await f.Items.FindAsync(item.ItemId, default))!; var escrow = r.Listing.EscrowAtRegistration;
        Check(source.State.Quantity == 6 && source.Revision == 2 && escrow.ItemId == AuctionEscrowRules.SplitItemId(command.ListingId)
            && escrow.State.Quantity == 4 && escrow.Revision == 1 && r.Listing.Split == new TradeSplitOrigin(item.ItemId, escrow.ItemId, 4), "partial split and provenance commit together");
        var theft = escrow with { Owner = item.Owner, Location = item.Location };
        Check((await f.Items.CommitAsync(new(Guid.NewGuid(), "Test.Theft", "test", item.Owner, 4, [new(1, theft)]), DateTimeOffset.UtcNow, default)).Status == ItemRepositoryCommitStatus.ValidationFailed, "normal source mutation cannot take escrow");
        var forged = item with { State = item.State with { InstanceId = Guid.NewGuid() }, Owner = escrow.Owner, Location = escrow.Location, Revision = 0 };
        var forge = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Test.Forge", "test", item.Owner, 1, [new(0, forged)]);
        Check((await f.Items.CommitAsync(forge, DateTimeOffset.UtcNow, default)).Status == ItemRepositoryCommitStatus.InvalidRequest, "normal PG cannot fabricate auction destination");
        Check((await new InMemoryItemRepository(new InMemoryTransactionGate()).CommitAsync(forge, DateTimeOffset.UtcNow, default)).Status == ItemRepositoryCommitStatus.InvalidRequest, "same new-destination guard in Memory");
        var malformed = forged with { Owner = new(ItemOwnerType.System, null!), Location = new(ItemContainerType.Inventory, item.Owner.OwnerId, 161) };
        Check(!ItemRecordRules.TryValidateCommit(forge with { Mutations = [new(0, malformed)] }, out _), "malformed owner is rejected without a new null dereference");
        await f.Store.ExecuteAsync(Close(r.Listing), default);
        Check((await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 10 && (await f.Items.FindAsync(escrow.ItemId, default))!.LifecycleState == ItemLifecycleState.Consumed, "return merges source and tombstones split identity");
        var tombstone = (await f.Items.FindAsync(escrow.ItemId, default))!;
        Check((await f.Items.CommitAsync(new(Guid.NewGuid(), "Test.Revive", "test", item.Owner, 1,
            [new(tombstone.Revision, tombstone with { Owner = item.Owner, Location = item.Location, LifecycleState = ItemLifecycleState.Active, State = tombstone.State with { Quantity = 1 } })]), DateTimeOffset.UtcNow, default)).Status == ItemRepositoryCommitStatus.ValidationFailed, "normal path cannot resurrect consumed vault identity");
        var next = (await f.Items.FindAsync(item.ItemId, default))!;
        var hold = new TradeReservationCommand(Guid.NewGuid(), Guid.NewGuid(), 1, p, "Gold", new([new(next.ItemId, next.Revision, 1)], 0), DateTimeOffset.UtcNow.AddMinutes(3));
        await new PostgresTradeReservationRepository(f.Data, Policy).ReserveAsync(hold, default);
        await Reject(() => f.Store.ExecuteAsync(Register(p, next, 1), default), "auction_item_reserved");
    }

    private static async Task Rollback(Fixture f)
    {
        var p = await f.Person(); var item = await f.Seed(p); var cmd = Register(p, item, 4);
        await f.FailReceipts(true);
        try { await f.Store.ExecuteAsync(cmd, default); throw new Exception("missing injected registration failure"); }
        catch (PostgresException e) when (e.SqlState == "P0001") { checks++; }
        finally { await f.FailReceipts(false); }
        Check((await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 10 && await f.Store.FindAsync(cmd.ListingId, p, default) is null
            && await f.Items.FindAsync(AuctionEscrowRules.SplitItemId(cmd.ListingId), default) is null, "registration receipt failure rolls back split/listing/owner");
        var r = await f.Store.ExecuteAsync(cmd, default); var close = Close(r.Listing);
        await f.FailReceipts(true);
        try { await f.Store.ExecuteAsync(close, default); throw new Exception("missing injected return failure"); }
        catch (PostgresException e) when (e.SqlState == "P0001") { checks++; }
        finally { await f.FailReceipts(false); }
        Check((await f.Store.FindAsync(cmd.ListingId, p, default))!.State.Revision == 1
            && (await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 6
            && (await f.Items.FindAsync(r.Listing.EscrowAtRegistration.ItemId, default))!.State.Quantity == 4, "return failure rolls back merge/tombstone/closed state");
        await f.Store.ExecuteAsync(close, default);
    }

    private static async Task Returns(Fixture f)
    {
        var p = await f.Person(); var item = await f.Seed(p); var r = await f.Store.ExecuteAsync(Register(p, item, 10), default);
        for (int slot = 160; slot < 240; ++slot) await f.Seed(p, 100, slot, seed: slot);
        var returned = await f.Store.ExecuteAsync(Close(r.Listing), default);
        Check(returned.Items.Single().Location.ContainerType == ItemContainerType.Mail && returned.Items.Single().ItemId == item.ItemId, "full material tab returns to mail with stable identity");
        var other = await f.Person(); var source = await f.Seed(other); var held = await f.Store.ExecuteAsync(Register(other, source, 10), default);
        // Three starter equipment records already occupy the account's current read projection.
        var tiny = new PostgresAuctionEscrowRepository(f.Data, Policy, new(10, 3));
        await Reject(() => tiny.ExecuteAsync(Close(held.Listing), default), "auction_return_capacity");
        Check((await f.Store.FindAsync(held.Listing.State.ListingId, other, default))!.State.Status == AuctionListingStatus.Active
            && (await f.Items.FindAsync(source.ItemId, default))!.Owner.Type == ItemOwnerType.System, "projection cap preserves pending return and escrow");
        await f.Store.ExecuteAsync(Close(held.Listing), default);
    }

    private static async Task Expiry(Fixture f)
    {
        var p = await f.Person(); var item = await f.Seed(p);
        var cmd = Register(p, item, 10) with { ExpiresAt = DateTimeOffset.UtcNow.AddSeconds(1) };
        var r = await f.Store.ExecuteAsync(cmd, default);
        await Reject(() => f.Store.ExecuteAsync(Close(r.Listing) with { Kind = AuctionEscrowKind.Expire }, default), "auction_not_expired");
        await Task.Delay(1100);
        var outcomes = await Task.WhenAll(TryClose(f, Close(r.Listing)), TryClose(f, Close(r.Listing) with { Kind = AuctionEscrowKind.Expire }));
        Check(outcomes.Count(x => x is not null) == 1 && outcomes.Single(x => x is not null)!.Listing.State.Status == AuctionListingStatus.Expired, "expired cancel/worker race has one CAS winner");
        var expiredItem = await f.Seed(p, slot: 161, expiry: DateTimeOffset.UtcNow.AddSeconds(2));
        var exp = await f.Store.ExecuteAsync(Register(p, expiredItem, 10) with { ExpiresAt = DateTimeOffset.UtcNow.AddSeconds(1) }, default);
        await Task.Delay(2200);
        var result = await f.Store.ExecuteAsync(Close(exp.Listing) with { Kind = AuctionEscrowKind.Expire }, default);
        Check(result.Items.Single().LifecycleState == ItemLifecycleState.Expired && result.Items.Single().State.Quantity == 0, "naturally expired escrow is not resurrected as inventory/mail");
    }

    private static async Task AccessAndLimits(Fixture f)
    {
        var p = await f.Person(); var item = await f.Seed(p);
        var one = new PostgresAuctionEscrowRepository(f.Data, Policy, new(1, 500));
        var command = Register(p, item, 3);
        var r = await one.ExecuteAsync(command, default);
        var remaining = (await f.Items.FindAsync(item.ItemId, default))!;
        await Reject(() => one.ExecuteAsync(Register(p, remaining, 1), default), "auction_listing_limit");
        await f.ServiceMode("Drain");
        try
        {
            Check(JsonSerializer.Serialize(r) == JsonSerializer.Serialize(await one.ExecuteAsync(command, default)), "exact receipt replay works during Drain");
            await one.ExecuteAsync(Close(r.Listing), default);
            try { await f.Store.ExecuteAsync(Register(p, (await f.Items.FindAsync(item.ItemId, default))!, 1), default); throw new Exception("Drain registration accepted"); }
            catch (ServiceDrainingException) { checks++; }
        }
        finally { await f.ServiceMode("Open"); }
        var held = await f.Store.ExecuteAsync(Register(p, (await f.Items.FindAsync(item.ItemId, default))!, 1), default);
        await f.Restrict(p);
        await f.Store.ExecuteAsync(Close(held.Listing), default);
        try { await f.Store.ExecuteAsync(Register(p, (await f.Items.FindAsync(item.ItemId, default))!, 1), default); throw new Exception("restriction accepted"); }
        catch (AccountRestrictedException) { checks++; }
    }
    private static async Task Races(Fixture f)
    {
        var p = await f.Person(); var item = await f.Seed(p); var cmd = Register(p, item, 3);
        var duplicate = await Task.WhenAll(f.Store.ExecuteAsync(cmd, default), f.Store.ExecuteAsync(cmd, default));
        Check(JsonSerializer.Serialize(duplicate[0]) == JsonSerializer.Serialize(duplicate[1]), "parallel same request one exact receipt");
        var close = Close(duplicate[0].Listing);
        var results = await Task.WhenAll(TryClose(f, close), TryClose(f, close with { RequestId = Guid.NewGuid() }));
        Check(results.Count(x => x is not null) == 1 && (await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 10, "parallel cancellation returns quantity once");
        var item2 = await f.Seed(await f.Person()); var actor = new TradeParticipant((await f.Game.GetCharacterEconomyContextAsync(Guid.Parse(item2.Owner.OwnerId), default))!.SteamId, Guid.Parse(item2.Owner.OwnerId));
        var first = Register(actor, item2, 2); var second = Register(actor, item2, 2);
        async Task<bool> Attempt(AuctionEscrowCommand c) { try { await f.Store.ExecuteAsync(c, default); return true; } catch (TradeRuleException e) when (e.Code == "item_revision_conflict") { return false; } }
        Check((await Task.WhenAll(Attempt(first), Attempt(second))).Count(x => x) == 1, "two listing IDs cannot split the same source revision twice");
        var mover = await f.Person(); var moving = await f.Seed(mover); var register = Register(mover, moving, 4);
        var move = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Test.Move", "test", moving.Owner, 0,
            [new(moving.Revision, moving with { Location = moving.Location with { SlotIndex = 161 } })]);
        async Task<bool> RegisterAttempt() { try { await f.Store.ExecuteAsync(register, default); return true; } catch (TradeRuleException e) when (e.Code == "item_revision_conflict") { return false; } }
        var listingTask = RegisterAttempt(); var moveTask = f.Items.CommitAsync(move, DateTimeOffset.UtcNow, default);
        await Task.WhenAll(listingTask, moveTask);
        Check((listingTask.Result ? 1 : 0) + (moveTask.Result.Status == ItemRepositoryCommitStatus.Committed ? 1 : 0) == 1,
            "normal inventory mutation and registration share owner lock/CAS with one winner");
    }

    private static async Task Reopen(Fixture f)
    {
        await using var c = await f.Data.OpenConnectionAsync(); await using var q = new NpgsqlCommand("SELECT command::text,result::text FROM auction_restart_fixture", c);
        AuctionEscrowCommand command; string expected;
        await using (var reader = await q.ExecuteReaderAsync()) { Check(await reader.ReadAsync(), "previous process fixture present"); command = JsonSerializer.Deserialize<AuctionEscrowCommand>(reader.GetString(0))!; expected = JsonSerializer.Serialize(JsonSerializer.Deserialize<AuctionEscrowResult>(reader.GetString(1))); }
        Check(JsonSerializer.Serialize(await f.Store.ExecuteAsync(command, default)) == expected, "new process exact receipt replay after returned/relisted item");
    }

    private static AuctionEscrowCommand Register(TradeParticipant p, ItemRecord item, int quantity) => new(Guid.NewGuid(), Guid.NewGuid(), p, AuctionEscrowKind.Register,
        Item: new(item.ItemId, item.Revision, quantity), TotalPrice: 699, ExpiresAt: DateTimeOffset.UtcNow.AddHours(1), Terms: Terms);
    private static AuctionEscrowCommand Close(AuctionEscrowListing listing) => new(Guid.NewGuid(), listing.State.ListingId, listing.State.Seller, AuctionEscrowKind.Cancel, listing.State.Revision);
    private static async Task<AuctionEscrowResult?> TryClose(Fixture f, AuctionEscrowCommand c) { try { return await f.Store.ExecuteAsync(c, default); } catch (TradeRuleException e) when (e.Code is "auction_revision_conflict" or "auction_not_active") { return null; } }
    private static void Check(bool value, string text) { if (!value) throw new Exception(text); ++checks; }
    private static async Task Reject(Func<Task> action, string code) { try { await action(); } catch (TradeRuleException e) when (e.Code == code) { ++checks; return; } throw new Exception("Expected " + code); }

    private sealed class Fixture(NpgsqlDataSource data)
    {
        public NpgsqlDataSource Data => data;
        public PostgresAuctionEscrowRepository Store { get; } = new(data, Policy, new(10, 500));
        public PostgresGameRepository Game { get; } = new(data, NullLogger<PostgresGameRepository>.Instance);
        public PostgresItemRepository Items { get; } = new(data, NullLogger<PostgresItemRepository>.Instance);
        public PostgresEconomyRepository Economy { get; } = new(data);
        public async Task<TradeParticipant> Person()
        {
            string account = (76561198490000000L + Interlocked.Increment(ref sequence)).ToString();
            await Game.UpsertAccountAsync(new(account, account, false, false), DateTimeOffset.UtcNow, default);
            var c = await Game.CreateCharacterAsync(account, "Auction" + sequence, DateTimeOffset.UtcNow, default); return new(account, c.CharacterId);
        }
        public async Task<ItemRecord> Seed(TradeParticipant p, int quantity = 10, int slot = 160, int seed = 1, DateTimeOffset? expiry = null)
        {
            var owner = new ItemOwnerRef(ItemOwnerType.Character, p.CharacterId.ToString("D"));
            var item = new ItemRecord("Material", "Ore", 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, slot),
                new(Guid.NewGuid(), seed, quantity, ["Item.Ore"], [new("Stat.Value", 4)]), 0, ItemLifecycleState.Active,
                new(ItemBindState.Unbound, new(0, 0), expiry, "Test", false));
            var result = await Items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "seed", owner, quantity, [new(0, item)]), DateTimeOffset.UtcNow, default);
            Check(result.Status == ItemRepositoryCommitStatus.Committed, "seed fixture"); return result.Records.Single();
        }
        public async Task Sql(string text, params (string Name, object Value)[] values)
        { await using var c = await data.OpenConnectionAsync(); await using var q = new NpgsqlCommand(text, c); foreach (var v in values) { if (v.Value is DBNull) q.Parameters.AddWithValue(v.Name, NpgsqlDbType.Varchar, DBNull.Value); else q.Parameters.AddWithValue(v.Name, v.Value); } await q.ExecuteNonQueryAsync(); }
        public Task FailReceipts(bool on) => Sql(on ? "CREATE FUNCTION auction_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected'; END $$; CREATE TRIGGER auction_test_fail BEFORE INSERT ON auction_escrow_receipts FOR EACH ROW EXECUTE FUNCTION auction_test_fail()" : "DROP TRIGGER auction_test_fail ON auction_escrow_receipts; DROP FUNCTION auction_test_fail()");
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
