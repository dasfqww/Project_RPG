using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class SessionCases
{
    private static int count, sequence;
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, false, 100)]);
    private static DateTimeOffset Now => DateTimeOffset.UtcNow;
    public static async Task<int> Run(bool reopen)
    {
        try
        {
            var cs = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES"));
            if (cs.Host != "127.0.0.1" || cs.Database != "equipment_test" || !Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_RUN"), "N", out _)) throw new Exception("Private test DB required");
            await using var data = NpgsqlDataSource.Create(cs.ConnectionString);
            Fixture f = new(data);
            if (reopen) { await Reopen(f); Console.WriteLine($"PASS Trade sessions separate-process restart: {count} assertions"); return 0; }
            var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
            Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog.Take(9).ToArray(), default) == 9, "v9 setup");
            var old = await f.Person();
            Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog, default) == 1, "v9 to v10 only");
            Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog, default) == 0, "migration replay");
            Check((await f.Game.GetCharacterEconomyContextAsync(old.CharacterId, default))!.SteamId == old.AccountId, "migration preserves character");
            await f.Economy.UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Account, 100000, true), Now, default);
            await Success(f); await Rollback(f); await Consent(f); await CapacityAndAccess(f); await Races(f);
            Console.WriteLine($"PASS PostgreSQL trade sessions: 5 groups, {count} assertions; no HTTP/UE/Memory-session claim."); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
    private static async Task Success(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); await f.Fund(a, 1000); await f.Fund(b, 2000);
        var ai = await f.ItemsFor(a, 80); var bi = await f.ItemsFor(b, 80, seedOffset: 1000);
        var s = await f.Open(a, b);
        s = await f.Offer(s, a, new([new(ai[7].ItemId, 1, 1)], 600));
        s = await f.Offer(s, b, new([new(bi[43].ItemId, 1, 1)], 200));
        s = await f.Ready(s, a); s = await f.Ready(s, b);
        Check(await f.Balance(a) == 1000 && (await f.Items.FindAsync(ai[7].ItemId, default))!.Owner == ai[7].Owner, "Ready holds without moving assets");
        Check((await f.Spend(a, -401)).Status == CurrencyTransactionStatus.InsufficientBalance, "normal spend respects session reservation");
        s = (await f.Do(s, a, TradeCommandKind.Confirm)).Session;
        var command = Cmd(s, b, TradeCommandKind.Confirm);
        var result = await f.Store.ExecuteAsync(command, default);
        Check(result.Session.Completed && result.Session.Exchange!.Currency.Count == 2, "final confirm completes exchange");
        Check(await f.Balance(a) == 600 && await f.Balance(b) == 2400, "money moved once");
        var moved = (await f.Items.FindAsync(ai[7].ItemId, default))!;
        Check(moved.Owner.OwnerId == b.CharacterId.ToString("D") && moved.Location.SlotIndex == 43 && moved.Revision == 2, "full bags swap with UNIQUE enabled");
        Check(await f.ActiveHolds(s.State.TradeId) == 0, "completion releases both holds");
        Check(JsonSerializer.Serialize(result) == JsonSerializer.Serialize(await f.Store.ExecuteAsync(command, default)), "exact response-loss replay");
        await Reject(() => f.Store.ExecuteAsync(command with { Kind = TradeCommandKind.Cancel }, default), "trade_idempotency_conflict");
        var eve = await f.Person(); Check(await f.Store.FindAsync(s.State.TradeId, eve, default) is null, "foreign query hidden");
        await Reject(() => f.Store.ExecuteAsync(command with { Actor = eve }, default), "trade_participant_required");
        await f.Sql("CREATE TABLE session_test_replay(command jsonb,result jsonb)");
        await using var c = await f.Data.OpenConnectionAsync();
        await using var q = new NpgsqlCommand("INSERT INTO session_test_replay VALUES(@c,@r)", c);
        q.Parameters.AddWithValue("c", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(command)); q.Parameters.AddWithValue("r", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(result)); await q.ExecuteNonQueryAsync();
    }
    private static async Task Reopen(Fixture f)
    {
        await using var c = await f.Data.OpenConnectionAsync();
        await using var q = new NpgsqlCommand("SELECT command::text,result::text FROM session_test_replay", c);
        await using var r = await q.ExecuteReaderAsync(); Check(await r.ReadAsync(), "previous process receipt present");
        var command = JsonSerializer.Deserialize<TradeSessionCommand>(r.GetString(0))!;
        var expected = JsonSerializer.Deserialize<TradeSessionResult>(r.GetString(1))!;
        var actual = await f.Store.ExecuteAsync(command, default);
        Check(JsonSerializer.Serialize(actual) == JsonSerializer.Serialize(expected), "new process reconstructs exact receipt and immutable offers/state");
        Check((await f.Store.FindAsync(command.TradeId, command.Actor, default))!.Completed, "new process finds completed session");
        Check(await f.Balance(expected.Session.State.Initiator) == 600, "new process replay does not transfer again");
    }
    private static async Task Rollback(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); await f.Fund(a, 100); await f.Fund(b, 100);
        var ai = (await f.ItemsFor(a, 1, 60))[0]; var bi = (await f.ItemsFor(b, 1, 90))[0];
        var s = await f.Open(a, b); s = await f.Offer(s, a, new([new(ai.ItemId, 1, 30)], 20));
        s = await f.Ready(s, a); s = await f.Ready(s, b); s = (await f.Do(s, a, TradeCommandKind.Confirm)).Session;
        var command = Cmd(s, b, TradeCommandKind.Confirm);
        await f.Sql("CREATE FUNCTION trade_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected receipt failure'; END $$; CREATE TRIGGER trade_test_fail BEFORE INSERT ON trade_session_receipts FOR EACH ROW EXECUTE FUNCTION trade_test_fail()");
        try { await f.Store.ExecuteAsync(command, default); throw new Exception("Missing injected failure"); }
        catch (PostgresException e) when (e.SqlState == "P0001") { count++; }
        finally { await f.Sql("DROP TRIGGER trade_test_fail ON trade_session_receipts; DROP FUNCTION trade_test_fail()"); }
        Check(await f.Balance(a) == 100 && await f.Balance(b) == 100, "receipt failure rolls back money");
        Check((await f.Items.FindAsync(ai.ItemId, default))!.State.Quantity == 60 && (await f.Items.FindAsync(bi.ItemId, default))!.State.Quantity == 90, "receipt failure rolls back staged slots/splits/merge");
        Check((await f.Store.FindAsync(s.State.TradeId, a, default))!.State.Revision == s.State.Revision && await f.ActiveHolds(s.State.TradeId) == 2, "failure rolls back final consent/release");
        var result = await f.Store.ExecuteAsync(command, default);
        Check(result.Session.Completed && result.Session.Exchange!.Splits.Count == 1, "same request succeeds after infrastructure recovery");
        Check((await f.Items.FindAsync(ai.ItemId, default))!.State.Quantity == 30 && (await f.Items.FindAsync(bi.ItemId, default))!.State.Quantity == 100, "split and merge actually persisted");
        var split = result.Session.Exchange!.Splits[0]; Check((await f.Items.FindAsync(split.SplitItemId, default))!.State.Quantity == 20, "split residual and provenance persisted");
    }
    private static async Task Consent(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); await f.Fund(a, 100); await f.Fund(b, 100);
        var s = await f.Open(a, b); s = await f.Offer(s, a, new([], 30));
        var ready = Cmd(s, a, TradeCommandKind.Ready);
        await f.Sql("CREATE FUNCTION trade_test_ready_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected session failure'; END $$; CREATE TRIGGER trade_test_ready_fail BEFORE UPDATE ON trade_sessions FOR EACH ROW EXECUTE FUNCTION trade_test_ready_fail()");
        try { await f.Store.ExecuteAsync(ready, default); throw new Exception("Missing failure"); } catch (PostgresException e) when (e.SqlState == "P0001") { count++; }
        finally { await f.Sql("DROP TRIGGER trade_test_ready_fail ON trade_sessions; DROP FUNCTION trade_test_ready_fail()"); }
        Check(await f.ActiveHolds(s.State.TradeId) == 0 && !(await f.Store.FindAsync(s.State.TradeId, a, default))!.State.InitiatorReady, "Ready and reservation rollback together");
        s = (await f.Store.ExecuteAsync(ready, default)).Session; s = await f.Ready(s, b);
        var old = Cmd(s, a, TradeCommandKind.Confirm);
        s = await f.Offer(s, a, new([], 25)); Check(!s.State.InviteeReady && await f.ActiveHolds(s.State.TradeId) == 0, "edit releases both sides and invalidates consent");
        await Reject(() => f.Store.ExecuteAsync(old, default), "trade_revision_conflict");
        s = await f.Ready(s, a); s = (await f.Do(s, b, TradeCommandKind.Withdraw)).Session;
        Check(await f.ActiveHolds(s.State.TradeId) == 0 && s.State.InitiatorOffer.CurrencyAmount == 25, "withdraw retains offer but releases holds");
        await Reject(() => f.Open(a, awaitable: b), "character_already_trading");
        s = await f.Ready(s, a);
        await f.Sql("UPDATE trade_sessions SET expires_at=clock_timestamp()-interval '1 second',payload=jsonb_set(payload,'{State,ExpiresAt}',to_jsonb(clock_timestamp()-interval '1 second')) WHERE trade_id=@id", ("id", s.State.TradeId));
        s = (await f.Do(s, b, TradeCommandKind.Expire)).Session;
        Check(s.State.Status == TradeNegotiationStatus.Expired && await f.ActiveHolds(s.State.TradeId) == 0, "expiry closes consent and releases reservation");
    }
    private static async Task CapacityAndAccess(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); await f.Fund(a, 100); await f.Fund(b, 100);
        var ai = (await f.ItemsFor(a, 1))[0]; await f.ItemsFor(b, 80, seedOffset: 1000);
        var s = await f.Open(a, b); s = await f.Offer(s, a, new([new(ai.ItemId, 1, 1)], 5));
        s = await f.Ready(s, a); s = await f.Ready(s, b); s = (await f.Do(s, a, TradeCommandKind.Confirm)).Session;
        var result = await f.Do(s, b, TradeCommandKind.Confirm);
        Check(result.Rejection == "trade_inventory_full" && !result.Session.Completed && await f.ActiveHolds(s.State.TradeId) == 0, "full recipient rejects all and withdraws holds");
        Check(await f.Balance(a) == 100 && (await f.Items.FindAsync(ai.ItemId, default))!.Revision == 1, "capacity rejection transfers nothing");
        s = await f.Offer(result.Session, a, new([], 5)); s = await f.Ready(s, a); s = await f.Ready(s, b);
        await f.ServiceMode("Drain");
        result = await f.Do(s, a, TradeCommandKind.Confirm);
        Check(result.Rejection == "service_draining" && await f.ActiveHolds(s.State.TradeId) == 0, "Drain rejects confirmation and releases holds");
        s = (await f.Do(result.Session, b, TradeCommandKind.Cancel)).Session; Check(s.State.Status == TradeNegotiationStatus.Cancelled, "cancel works during Drain");
        await f.ServiceMode("Open");
        s = await f.Open(a, b); s = await f.Offer(s, a, new([], 5)); s = await f.Ready(s, a); s = await f.Ready(s, b);
        await f.Restrict(a);
        result = await f.Do(s, b, TradeCommandKind.Confirm);
        Check(result.Rejection == "account_restricted" && await f.ActiveHolds(s.State.TradeId) == 0, "restriction of other side prevents exchange");
        s = (await f.Do(result.Session, b, TradeCommandKind.Cancel)).Session; Check(s.State.Status == TradeNegotiationStatus.Cancelled, "other side can cancel restricted participant");
    }
    private static async Task Races(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); await f.Fund(a, 100); await f.Fund(b, 100);
        var s = await f.Open(a, b); s = await f.Offer(s, a, new([], 20));
        var ready = Cmd(s, a, TradeCommandKind.Ready);
        var duplicate = await Task.WhenAll(f.Store.ExecuteAsync(ready, default), f.Store.ExecuteAsync(ready, default));
        Check(JsonSerializer.Serialize(duplicate[0]) == JsonSerializer.Serialize(duplicate[1]) && await f.ActiveHolds(s.State.TradeId) == 1, "concurrent identical Ready has one hold/receipt");
        s = await f.Ready(duplicate[0].Session, b); s = (await f.Do(s, a, TradeCommandKind.Confirm)).Session;
        var confirm = Cmd(s, b, TradeCommandKind.Confirm); var cancel = Cmd(s, a, TradeCommandKind.Cancel);
        async Task<TradeSessionResult?> Attempt(TradeSessionCommand cmd) { try { return await f.Store.ExecuteAsync(cmd, default); } catch (TradeRuleException e) when (e.Code is "trade_revision_conflict" or "trade_closed") { return null; } }
        var outcomes = await Task.WhenAll(Attempt(confirm), Attempt(cancel));
        Check(outcomes.Count(x => x is not null) == 1, "final confirm/cancel has exactly one winner");
        var final = (await f.Store.FindAsync(s.State.TradeId, a, default))!;
        Check(await f.Balance(a) == (final.Completed ? 80 : 100) && await f.ActiveHolds(s.State.TradeId) == 0, "race never partially settles");
        // Real admission holds the same account gate. The final confirmation must wait and then observe its lease.
        s = await f.Open(a, b); s = await f.Offer(s, a, new([], 5)); s = await f.Ready(s, a); s = await f.Ready(s, b); s = (await f.Do(s, a, TradeCommandKind.Confirm)).Session;
        await using var c = await f.Data.OpenConnectionAsync(); await using var tx = await c.BeginTransactionAsync();
        await using (var gate = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", c, tx)) { gate.Parameters.AddWithValue("key", "ops_account:" + a.AccountId); await gate.ExecuteNonQueryAsync(); }
        var pending = f.Do(s, b, TradeCommandKind.Confirm); await Task.Delay(100); Check(!pending.IsCompleted, "confirmation waits behind admission account gate");
        // Insert a real session + lease with the same schema as CreateDungeonSessionAsync, then commit admission.
        var dungeon = Guid.NewGuid();
        await using (var lease = new NpgsqlCommand("INSERT INTO dungeon_sessions(dungeon_session_id,dungeon_id,difficulty,state,created_at,updated_at,expires_at,leader_character_id,lobby_revision) VALUES(@d,'Test','Normal','Waiting',clock_timestamp(),clock_timestamp(),clock_timestamp()+interval '1 hour',@a,1); INSERT INTO character_session_leases VALUES(@a,@d,clock_timestamp()+interval '1 hour',clock_timestamp())", c, tx))
        { lease.Parameters.AddWithValue("d", dungeon); lease.Parameters.AddWithValue("a", a.CharacterId); await lease.ExecuteNonQueryAsync(); }
        await tx.CommitAsync(); var blocked = await pending;
        Check(blocked.Rejection == "trade_requires_lobby" && await f.ActiveHolds(s.State.TradeId) == 0, "committed admission wins; final consent cannot transfer");
        var p = await f.Person(); var q = await f.Person(); await f.Fund(p, 50); await f.Fund(q, 50);
        var other = await f.Open(p,q); other = await f.Offer(other,p,new([],10)); other=await f.Ready(other,p);other=await f.Ready(other,q);
        await f.Game.CreateDungeonSessionAsync(p.AccountId,p.CharacterId,"Test","Normal",Now,Now.AddMinutes(5),default);
        var admission = await f.Do(other,q,TradeCommandKind.Confirm);
        Check(admission.Rejection == "trade_requires_lobby" && await f.Balance(p)==50, "actual game admission repository excludes settlement");
        var x = await f.Person(); var y=await f.Person();await f.Fund(x,50);await f.Fund(y,50);
        var exp = await f.Open(x,y);exp=await f.Offer(exp,x,new([],10));exp=await f.Ready(exp,x);exp=await f.Ready(exp,y);exp=(await f.Do(exp,x,TradeCommandKind.Confirm)).Session;
        await using var tx2 = await c.BeginTransactionAsync();
        await using (var gate = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))",c,tx2)) {gate.Parameters.AddWithValue("key","ops_account:"+x.AccountId);await gate.ExecuteNonQueryAsync();}
        await f.Sql("UPDATE trade_sessions SET expires_at=clock_timestamp()+interval '200 milliseconds',payload=jsonb_set(payload,'{State,ExpiresAt}',to_jsonb(clock_timestamp()+interval '200 milliseconds')) WHERE trade_id=@id",("id",exp.State.TradeId));
        var late=f.Do(exp,y,TradeCommandKind.Confirm);await Task.Delay(350);await tx2.CommitAsync();var expired=await late;
        Check(expired.Session.State.Status==TradeNegotiationStatus.Expired && !expired.Session.Completed && await f.Balance(x)==50 && await f.ActiveHolds(exp.State.TradeId)==0,"expiry after account lock wait closes without settling");
    }
    private static TradeSessionCommand Cmd(TradeStoredSession s, TradeParticipant p, TradeCommandKind kind) => new(Guid.NewGuid(), s.State.TradeId, p, kind, s.State.Revision, s.State.OfferRevision);
    private static void Check(bool value, string text) { if (!value) throw new Exception(text); count++; }
    private static async Task Reject(Func<Task> action, string code) { try { await action(); } catch (TradeRuleException e) when (e.Code == code) { count++; return; } throw new Exception("Expected " + code); }
    private sealed class Fixture(NpgsqlDataSource data)
    {
        public NpgsqlDataSource Data => data;
        public PostgresTradeSessionRepository Store { get; } = new(data, Policy);
        public PostgresItemRepository Items { get; } = new(data, NullLogger<PostgresItemRepository>.Instance);
        public PostgresEconomyRepository Economy { get; } = new(data);
        public PostgresGameRepository Game { get; } = new(data, NullLogger<PostgresGameRepository>.Instance);
        public async Task<TradeParticipant> Person() {
            int i = Interlocked.Increment(ref sequence); string account = (76561198200000000L+i).ToString();
            await Game.UpsertAccountAsync(new(account, account, false, false), Now, default);
            var p = await Game.CreateCharacterAsync(account, "Session"+i, Now, default); return new(account, p.CharacterId);
        }
        public async Task<CurrencyTransactionResult> Spend(TradeParticipant p, long amount) => await Economy.CommitAsync(new(Guid.NewGuid(), p.CharacterId, Guid.NewGuid(), "Test.Fund", Guid.NewGuid().ToString("N"), "test", [new("Gold", amount)]), (await Game.GetCharacterEconomyContextAsync(p.CharacterId, default))!, Now, default);
        public async Task Fund(TradeParticipant p, long amount) => Check((await Spend(p, amount)).Status == CurrencyTransactionStatus.Committed, "fund fixture");
        public async Task<long> Balance(TradeParticipant p) => (await Economy.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(p.CharacterId, default))!, default)).Balances.Single(v => v.Definition.CurrencyCode == "Gold").Balance;
        public async Task<ItemRecord[]> ItemsFor(TradeParticipant p, int count, int quantity = 1, int seedOffset = 0) {
            var owner = new ItemOwnerRef(ItemOwnerType.Character, p.CharacterId.ToString("D")); List<ItemRecord> result = [];
            for(int slot=0;slot<count;slot++) { ItemRecord item = new("Material", "Ore", 1, owner, new(ItemContainerType.Inventory,owner.OwnerId,slot), new(Guid.NewGuid(), quantity == 1 ? slot+seedOffset+1 : 1,quantity,[],[]),0,ItemLifecycleState.Active,new(ItemBindState.Unbound,new(0,0),null,"Test",false));
                var r=await Items.CommitAsync(new(Guid.NewGuid(),"Test.Seed",Guid.NewGuid().ToString("N"),owner,quantity,[new(0,item)]),Now,default); Check(r.Status==ItemRepositoryCommitStatus.Committed,"seed item"); result.Add(r.Records.Single()); }
            return result.ToArray();
        }
        public async Task<TradeStoredSession> Open(TradeParticipant a, TradeParticipant awaitable) {
            var invite=await Store.ExecuteAsync(new(Guid.NewGuid(),Guid.NewGuid(),a,TradeCommandKind.Invite,Invitee:awaitable,CurrencyCode:"Gold"),default);
            return (await Do(invite.Session,awaitable,TradeCommandKind.Accept)).Session;
        }
        public async Task<TradeStoredSession> Offer(TradeStoredSession s, TradeParticipant p, TradeOffer offer) => (await Store.ExecuteAsync(Cmd(s,p,TradeCommandKind.ReplaceOffer) with { Offer=offer },default)).Session;
        public async Task<TradeStoredSession> Ready(TradeStoredSession s, TradeParticipant p) { var r=await Do(s,p,TradeCommandKind.Ready); Check(r.Rejection is null,"Ready succeeds"); return r.Session; }
        public Task<TradeSessionResult> Do(TradeStoredSession s,TradeParticipant p,TradeCommandKind k)=>Store.ExecuteAsync(Cmd(s,p,k),default);
        public async Task<long> ActiveHolds(Guid id) { await using var c=await data.OpenConnectionAsync(); await using var q=new NpgsqlCommand("SELECT count(*) FROM trade_asset_reservations WHERE trade_id=@id AND state='Active' AND expires_at>clock_timestamp()",c);q.Parameters.AddWithValue("id",id);return (long)(await q.ExecuteScalarAsync())!; }
        public async Task Sql(string sql, params (string Name,object Value)[] args) { await using var c=await data.OpenConnectionAsync(); await using var q=new NpgsqlCommand(sql,c);foreach(var a in args) { if(a.Value is DBNull) q.Parameters.AddWithValue(a.Name,NpgsqlDbType.Varchar,DBNull.Value);else q.Parameters.AddWithValue(a.Name,a.Value); } await q.ExecuteNonQueryAsync(); }
        private async Task<Guid> Audit(string scope,string? account=null) {
            Guid id=Guid.NewGuid(); await Sql("INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,target_scope,target_account_id,reason,committed_at,result_json,origin_http_request_id) VALUES(@id,'Test',decode(repeat('00',32),'hex'),'{}','test',@scope,@account,'test controlled fixture',clock_timestamp(),'{}',@id)",("id",id),("scope",scope),("account",(object?)account??DBNull.Value));return id;
        }
        public async Task ServiceMode(string mode) { var id=await Audit("Service"); await Sql("UPDATE ops_service_control SET mode=@mode,revision=revision+1,updated_at=clock_timestamp(),action_request_id=@id WHERE id=1",("mode",mode),("id",id)); }
        public async Task Restrict(TradeParticipant p) { var id=await Audit("Account",p.AccountId);await Sql("INSERT INTO ops_account_restrictions VALUES(@account,1,'Active',clock_timestamp(),clock_timestamp()+interval '1 hour','test controlled fixture',@id)",("account",p.AccountId),("id",id)); }
    }
}
