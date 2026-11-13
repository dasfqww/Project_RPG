using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Riding;
using ProjectRpg.Backend.Operations.ServiceControl;
using ProjectRpg.Backend.Operations.Restrictions;
using ProjectRpg.Operations.Core.Restrictions;

static class Program
{
    static NpgsqlDataSource Source = null!;
    static int assertions, sequence;
    static readonly List<object> cases = [];
    static RidingCatalog Catalog(int version = 7) => new(1, version, new(), [new("horse", 1, true, [])],
        [new(new("RPGItemDefinition", "GameItem.Consume.Mount.Horse", 1), "horse", 99, RidingItemUse.RegisterMount)]);
    static PostgresItemRepository Items() => new(Source, NullLogger<PostgresItemRepository>.Instance);
    static PostgresGameRepository Game() => new(Source, NullLogger<PostgresGameRepository>.Instance, equipmentItems: Items());
    sealed record Fixture(string Account, GameCharacter Character, ItemRecord Item, RidingRegistrationCommand Command)
    {
        public Task<RidingRegistrationResult> Register(RidingRegistrationCommand? command = null, RidingCatalog? catalog = null) =>
            Game().RegisterMountAsync(Account, Character.CharacterId, command ?? Command, catalog ?? Catalog(), default);
        public Task<RidingCollection> Collection() => Game().GetRidingCollectionAsync(Account, Character.CharacterId, default);
        public Task<RidingRegistrationReceipt?> Receipt() => Game().GetRidingRegistrationReceiptAsync(Account, Character.CharacterId, Command.RequestId, default);
    }
    static async Task<Fixture> New(int quantity = 2, string? account = null)
    {
        var now = DateTimeOffset.UtcNow; int n = Interlocked.Increment(ref sequence);
        account ??= (76561900000000000L + n).ToString(); var game = Game();
        await game.UpsertAccountAsync(new(account, account, false, false), now, default);
        var character = await game.CreateCharacterAsync(account, "RidingPg" + n, now, default);
        string owner = character.CharacterId.ToString("D");
        var item = new ItemRecord("RPGItemDefinition", "GameItem.Consume.Mount.Horse", 1, new(ItemOwnerType.Character, owner),
            new(ItemContainerType.Inventory, owner, 80), new(Guid.NewGuid(), 0, quantity, ["Item.Rarity.Rare"], []), 0,
            ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), now.AddDays(1), "Test", false));
        var result = await Items().CommitAsync(new(Guid.NewGuid(), "Test.Seed", "seed", item.Owner, quantity, [new(0, item)]), now, default);
        Check(result.Status == ItemRepositoryCommitStatus.Committed, "Seed through common Item V2 store"); item = result.Records.Single();
        return new(account, character, item, new(Guid.NewGuid(), item.ItemId, 1, 7));
    }
    static void Check(bool value, string reason) { assertions++; if (!value) throw new InvalidOperationException(reason); }
    static async Task Reject<T>(Func<Task> action, Func<T, bool>? match = null) where T : Exception
    { assertions++; try { await action(); } catch (T error) when (match is null || match(error)) { return; } throw new InvalidOperationException("Expected " + typeof(T).Name); }
    static Task StoreReject(RidingStoreFailure code, Func<Task> action) => Reject<RidingStoreException>(action, e => e.Code == code);
    static async Task Case(string name, Func<Task> action)
    { int before = assertions; await action(); cases.Add(new { Name = name, Assertions = assertions - before, Passed = true }); }
    static async Task<int> Sql(string text, params (string Name, object Value)[] parameters)
    { await using var c = await Source.OpenConnectionAsync(); await using var q = new NpgsqlCommand(text, c); foreach (var p in parameters) q.Parameters.AddWithValue(p.Name, p.Value); return await q.ExecuteNonQueryAsync(); }
    static async Task<long> Count(string text, params (string Name, object Value)[] parameters)
    { await using var c = await Source.OpenConnectionAsync(); await using var q = new NpgsqlCommand(text, c); foreach (var p in parameters) q.Parameters.AddWithValue(p.Name, p.Value); return Convert.ToInt64(await q.ExecuteScalarAsync()); }
    static async Task<ItemRecord> Current(Fixture f) => (await Items().FindAsync(f.Item.ItemId, default))!;
    static PostgresTradeReservationRepository Reservations() => new(Source, new([new("RPGItemDefinition", "GameItem.Consume.Mount.Horse", 1, true, true, 99)]));
    static TradeReservationCommand Hold(Fixture f) => new(Guid.NewGuid(), Guid.NewGuid(), 1, new(f.Account, f.Character.CharacterId), "Gold",
        new([new(f.Item.ItemId, 1, 1)], 0), DateTimeOffset.UtcNow.AddHours(1));
    static async Task MainCases()
    {
        await Case("pg_atomic_tombstone_and_replay", async () =>
        {
            var f = await New(1); var result = await f.Register(); var item = await Current(f); var collection = await f.Collection();
            Check(!result.Replayed && result.Receipt.Outcome == RidingRegistrationOutcome.Registered, "New command committed");
            Check(item.Revision == 2 && item.State.Quantity == 0 && item.LifecycleState == ItemLifecycleState.Consumed && item.Location.ContainerType == ItemContainerType.Terminal, "Real terminal consume/revision");
            Check(collection.Revision == 1 && collection.Mounts.Single().SourceRequestId == f.Command.RequestId, "Unlock provenance and revision");
            var replay = await f.Register(catalog: Catalog(8));
            Check(replay.Replayed && JsonSerializer.Serialize(result.Receipt) == JsonSerializer.Serialize(replay.Receipt), "Original payload survives missing item and catalog deployment");
            Check(JsonSerializer.Serialize(await f.Receipt()) == JsonSerializer.Serialize(result.Receipt), "Queryable linked receipt");
            await File.WriteAllTextAsync(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_RESTART_PROOF")!, JsonSerializer.Serialize(new Restart(f.Account, f.Character.CharacterId, f.Command, result.Receipt)));
        });
        await Case("pg_same_request_concurrency", async () =>
        {
            var f = await New(); var results = await Task.WhenAll(Enumerable.Range(0, 12).Select(_ => f.Register()));
            Check(results.Count(r => !r.Replayed) == 1, "Twelve connections have one commit");
            Check(results.Select(r => JsonSerializer.Serialize(r.Receipt)).Distinct().Count() == 1, "Exact first outcome replayed");
            Check((await Current(f)).State.Quantity == 1 && (await f.Collection()).Revision == 1, "Exactly one consume/unlock");
        });
        await Case("pg_shared_roster_concurrency", async () =>
        {
            var a = await New(); var b = await New(account: a.Account); var results = await Task.WhenAll(a.Register(), b.Register());
            Check(results.Count(r => r.Receipt.Outcome == RidingRegistrationOutcome.Registered) == 1 && results.Count(r => r.Receipt.Outcome == RidingRegistrationOutcome.AlreadyOwned) == 1, "Alternate character converges");
            Check((await Current(a)).State.Quantity + (await Current(b)).State.Quantity == 3 && (await a.Collection()).Revision == 1, "One roster update/consume");
            var noop = results.Single(r => r.Receipt.Outcome == RidingRegistrationOutcome.AlreadyOwned).Receipt;
            Check(noop.ItemReceipt is null && await Count("SELECT count(*) FROM item_transaction_receipts WHERE request_id=@id", ("id", RidingRegistrationRules.ChildItemRequestId(noop.RequestId))) == 0, "AlreadyOwned has no child");
            Check((await a.Register()).Replayed && (await b.Register()).Replayed, "Both parent receipts retained");
        });
        await Case("pg_scope_cas_and_expiry", async () =>
        {
            var f = await New(); await f.Register();
            await StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Register(f.Command with { ExpectedItemRevision = 2 }));
            await StoreReject(RidingStoreFailure.CharacterNotOwned, () => Game().RegisterMountAsync("76561999999999999", f.Character.CharacterId, f.Command, Catalog(), default));
            var other = await New(account: f.Account);
            await StoreReject(RidingStoreFailure.IdempotencyConflict, () => Game().GetRidingRegistrationReceiptAsync(f.Account, other.Character.CharacterId, f.Command.RequestId, default));
            await Reject<RidingRuleException>(() => f.Register(f.Command with { RequestId = Guid.NewGuid() }), e => e.Code == RidingFailure.RevisionConflict);
            var expired = await New(); await Sql("UPDATE item_records SET expires_at=clock_timestamp()-interval '1 second' WHERE item_id=@id", ("id", expired.Item.ItemId));
            await Reject<RidingRuleException>(() => expired.Register(), e => e.Code == RidingFailure.ItemExpired);
            Check((await expired.Collection()).Revision == 0, "Expiry from current DB clock has no writes");
        });
        await Case("pg_reservation_and_expiry", async () =>
        {
            await new PostgresEconomyRepository(Source).UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Character, 1000000, true), DateTimeOffset.UtcNow, default);
            var f = await New(); var hold = Hold(f); await Reservations().ReserveAsync(hold, default);
            await Reject<RidingRuleException>(() => f.Register(), e => e.Code == RidingFailure.AssetReserved);
            Check((await Current(f)).Revision == 1 && await f.Receipt() is null, "Reservation prevents all writes");
            await Sql("UPDATE trade_asset_reservations SET expires_at=clock_timestamp()-interval '1 second' WHERE reservation_id=@id", ("id", hold.ReservationId));
            Check((await f.Register()).Receipt.Outcome == RidingRegistrationOutcome.Registered, "Expired reservation needs no cleanup worker");
        });
        await Case("pg_reservation_registration_race", async () =>
        {
            for (int n = 0; n < 5; n++)
            {
                var f = await New(); var hold = Hold(f);
                async Task<string> Register() { try { await f.Register(); return "registered"; } catch (RidingRuleException e) when (e.Code == RidingFailure.AssetReserved) { return "reserved"; } }
                async Task<string> Reserve() { try { await Reservations().ReserveAsync(hold, default); return "held"; } catch (TradeRuleException e) when (e.Code == "item_revision_conflict") { return "stale"; } }
                var result = await Task.WhenAll(Register(), Reserve());
                Check(result.SequenceEqual(new[] { "registered", "stale" }) || result.SequenceEqual(new[] { "reserved", "held" }), "Shared real owner/item locks serialize reservation");
                Check((await Current(f)).State.Quantity == (result[0] == "registered" ? 1 : 2), "Race never double consumes");
            }
        });
        await Case("pg_lease_nowait_and_unsettled", async () =>
        {
            var f = await New(); var now = DateTimeOffset.UtcNow;
            var session = await Game().CreateDungeonSessionAsync(f.Account, f.Character.CharacterId, "fixture", "Normal", now, now.AddHours(1), default);
            await StoreReject(RidingStoreFailure.RequiresLobby, () => f.Register());
            await Sql("UPDATE character_session_leases SET expires_at=clock_timestamp()-interval '1 second' WHERE character_id=@id; UPDATE dungeon_sessions SET state='Closed' WHERE dungeon_session_id=@session", ("id", f.Character.CharacterId), ("session", session.DungeonSessionId));
            await using (var c = await Source.OpenConnectionAsync())
            await using (var tx = await c.BeginTransactionAsync())
            {
                await using var lease = new NpgsqlCommand("SELECT character_id FROM character_session_leases WHERE character_id=@id FOR UPDATE", c, tx); lease.Parameters.AddWithValue("id", f.Character.CharacterId); await lease.ExecuteScalarAsync();
                await StoreReject(RidingStoreFailure.RequiresLobby, () => f.Register()); await tx.RollbackAsync();
            }
            await Sql("""
                INSERT INTO dungeon_reward_settlements(dungeon_session_id,server_id,reward_version,command_fingerprint,state,character_ids,currency_changes,item_rewards,next_attempt_at,created_at,updated_at)
                VALUES(@session,'fixture','fixture',repeat('a',64),'Pending',@member::jsonb,'[]','[]',clock_timestamp(),clock_timestamp(),clock_timestamp());
                """, ("session", session.DungeonSessionId), ("member", JsonSerializer.Serialize(new[] { f.Character.CharacterId })));
            await StoreReject(RidingStoreFailure.RequiresLobby, () => f.Register());
            await Sql("UPDATE dungeon_reward_settlements SET state='Processing' WHERE dungeon_session_id=@id", ("id", session.DungeonSessionId));
            await StoreReject(RidingStoreFailure.RequiresLobby, () => f.Register());
            await Sql("UPDATE dungeon_reward_settlements SET state='Completed' WHERE dungeon_session_id=@id", ("id", session.DungeonSessionId));
            Check((await f.Register()).Receipt.Outcome == RidingRegistrationOutcome.Registered, "Expired unlocked lease and completed settlement allow lobby write");
        });
        await Case("pg_child_collision", async () =>
        {
            var f = await New(); var childId = RidingRegistrationRules.ChildItemRequestId(f.Command.RequestId);
            await Items().CommitAsync(new(childId, "Test.Foreign", "foreign", f.Item.Owner, 0, [new(1, f.Item)]), DateTimeOffset.UtcNow, default);
            await StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Register(f.Command with { ExpectedItemRevision = 2 }));
            Check((await Current(f)).State.Quantity == 2 && (await f.Collection()).Revision == 0 && await f.Receipt() is null, "Orphan common child cannot be adopted");
        });
        await Case("pg_rollback_after_item_and_unlock", async () =>
        {
            var f = await New();
            await Sql($"""
                CREATE FUNCTION riding_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN
                  IF NEW.request_id='{f.Command.RequestId:D}'::uuid THEN RAISE EXCEPTION 'riding_test_rollback' USING ERRCODE='23514'; END IF; RETURN NEW; END $$;
                CREATE TRIGGER riding_test_fail BEFORE INSERT ON riding_command_receipts FOR EACH ROW EXECUTE FUNCTION riding_test_fail();
                """);
            try { await Reject<PostgresException>(() => f.Register(), e => e.SqlState == "23514"); }
            finally { await Sql("DROP TRIGGER riding_test_fail ON riding_command_receipts; DROP FUNCTION riding_test_fail()"); }
            Check((await Current(f)).Revision == 1 && (await Current(f)).State.Quantity == 2 && (await f.Collection()).Revision == 0, "Actual child/item/unlock reverted");
            Check(await f.Receipt() is null && await Items().TryGetCommitResultAsync(RidingRegistrationRules.ChildItemRequestId(f.Command.RequestId), default) is null, "Both receipts reverted");
            Check((await f.Register()).Receipt.Outcome == RidingRegistrationOutcome.Registered, "Same command retries after DB rollback");
        });
        await Case("pg_admission_absent_lease_serialization", async () =>
        {
            var f = await New(); const long key = 187641;
            await using var control = await Source.OpenConnectionAsync(); await using var held = await control.BeginTransactionAsync();
            await using (var q = new NpgsqlCommand($"SELECT pg_advisory_xact_lock({key})", control, held)) await q.ExecuteNonQueryAsync();
            await Sql($"""
                CREATE FUNCTION riding_test_wait() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN
                  IF NEW.request_id='{f.Command.RequestId:D}'::uuid THEN PERFORM pg_advisory_xact_lock({key}); END IF; RETURN NEW; END $$;
                CREATE TRIGGER riding_test_wait BEFORE INSERT ON riding_command_receipts FOR EACH ROW EXECUTE FUNCTION riding_test_wait();
                """);
            Task<RidingRegistrationResult>? registration = null; Task<DungeonSession>? admission = null; bool released = false;
            try
            {
                registration = f.Register(); bool waiting = false;
                for (int i = 0; i < 100; i++) { if (await Count("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND objid=@key::oid AND NOT granted", ("key", (int)key)) > 0) { waiting = true; break; } await Task.Delay(20); }
                Check(waiting, "Register paused after real item/unlock writes with absent original lease");
                Check((await Current(f)).State.Quantity == 2, "Uncommitted consumption invisible from another connection");
                var now = DateTimeOffset.UtcNow;
                admission = Game().CreateDungeonSessionAsync(f.Account, f.Character.CharacterId, "fixture", "Normal", now, now.AddHours(1), default);
                Check(await Task.WhenAny(admission, Task.Delay(150)) != admission, "Admission cannot insert absent lease past held character/account boundary");
                await held.CommitAsync(); released = true; await registration; await admission;
                Check((await f.Register()).Replayed, "Admitted caller still recovers original success");
                await StoreReject(RidingStoreFailure.RequiresLobby, () => f.Register(f.Command with { RequestId = Guid.NewGuid(), ExpectedItemRevision = 2 }));
            }
            finally { if (!released) await held.RollbackAsync(); if (registration is not null) try { await registration; } catch { } if (admission is not null) try { await admission; } catch { } await Sql("DROP TRIGGER riding_test_wait ON riding_command_receipts; DROP FUNCTION riding_test_wait()"); }
        });
        await Case("pg_service_replay_and_world_isolation", async () =>
        {
            var f = await New(); await f.Register();
            var service = new PostgresServiceControlStore(Source, TimeProvider.System);
            var snapshot = (await service.ReadAsync(default)).Value;
            Check((await service.CommitAsync(new(new(Guid.NewGuid(), snapshot.Revision, "Drain", "", null, null, "Riding service fixture"), "fixture", Guid.NewGuid(), null), default)).IsSuccess, "Real service store enters Drain");
            try { Check((await f.Register()).Replayed && await f.Receipt() is not null, "Service closure cannot rewrite success"); await Reject<ServiceDrainingException>(() => f.Register(f.Command with { RequestId = Guid.NewGuid(), ExpectedItemRevision = 2 })); }
            finally { var current = (await service.ReadAsync(default)).Value; Check((await service.CommitAsync(new(new(Guid.NewGuid(), current.Revision, "Open", "", null, null, "Riding service fixture"), "fixture", Guid.NewGuid(), null), default)).IsSuccess, "Restore service through real store"); }
            var other = await New(account: f.Account); var roster = Guid.NewGuid();
            await Sql("INSERT INTO rosters(roster_id,steam_id,world_id,created_at,updated_at) VALUES(@roster,@account,'other',clock_timestamp(),clock_timestamp()); UPDATE characters SET roster_id=@roster WHERE character_id=@id", ("roster", roster), ("account", f.Account), ("id", other.Character.CharacterId));
            Check((await other.Collection()).Revision == 0, "Account does not imply cross-world ownership");
            Check((await other.Register()).Receipt.Outcome == RidingRegistrationOutcome.Registered && (await f.Collection()).Revision == 1, "Independent world unlock preserves original roster");
            var restrictions = new PostgresRestrictionStore(Source, TimeProvider.System);
            Check((await restrictions.CommitAsync(new(new(Guid.NewGuid(), f.Account, 0, DateTimeOffset.UtcNow.AddHours(1), "Riding restriction fixture"), "fixture", Guid.NewGuid(), null), default)).IsSuccess, "Current account restriction committed through real store");
            await Reject<AccountRestrictedException>(() => f.Register());
            await Reject<AccountRestrictedException>(() => f.Receipt());
            await Reject<AccountRestrictedException>(() => f.Collection());
        });
        await Case("pg_schema_packaging_and_constraints", async () =>
        {
            var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
            Check(catalog.Count == 16 && catalog[^1].Name == "riding_preferences", "Packaged migration catalog includes v16");
            Check(await Count("SELECT count(*) FROM pg_constraint WHERE conrelid IN ('riding_roster_state'::regclass,'riding_command_receipts'::regclass,'riding_mount_unlocks'::regclass) AND contype='p'") == 3, "Persistent uniqueness enforced");
            await Game().EnsureCreatedAsync(default); Check(await Count("SELECT count(*) FROM riding_command_receipts") > 0, "Idempotent initialization preserves receipts");
        });
        await Case("pg_upgrade_v15_to_v16", async () =>
        {
            var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
            await Sql("CREATE DATABASE riding_upgrade");
            var builder = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_TEST_POSTGRES")!) { Database = "riding_upgrade" };
            await using var upgrade = NpgsqlDataSource.Create(builder.ConnectionString);
            Check(await PostgresSchemaMigrations.ApplyAsync(upgrade, catalog.Take(15).ToArray(), default) == 15, "Real deployed v15 history created");
            var originalSource = Source;
            try
            {
                Source = upgrade; var f = await New(); var original = await f.Register();
                Check(await PostgresSchemaMigrations.ApplyAsync(upgrade, catalog, default) == 1, "Only migration16 applied over existing v15 registration");
                Check((await Current(f)).Revision == 2 && (await Current(f)).State.Quantity == 1, "Upgrade preserves existing item state");
                Check(JsonSerializer.Serialize((await f.Register()).Receipt) == JsonSerializer.Serialize(original.Receipt), "Upgraded store retains linked original registration receipt");
                var prefs = await Game().SaveRidingPreferencesAsync(f.Account, f.Character.CharacterId, new(Guid.NewGuid(), 0, "horse", ["horse"], new(RidingShortcutMode.Specific, "horse")), Catalog(), default);
                Check(prefs.Receipt.Preferences.Revision == 1, "New settings work after upgrade");
                Check(await PostgresSchemaMigrations.ApplyAsync(upgrade, catalog, default) == 0, "Deployment retry makes no changes");
                await PostgresSchemaMigrations.ValidateAsync(upgrade, catalog, default);
            }
            finally { Source = originalSource; }
        });
    }
    sealed record Restart(string Account, Guid CharacterId, RidingRegistrationCommand Command, RidingRegistrationReceipt Receipt);
    static async Task<int> Main(string[] args)
    {
        try
        {
            string connection = Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_TEST_POSTGRES") ?? throw new InvalidOperationException("Private PG required");
            await using var source = NpgsqlDataSource.Create(connection); Source = source;
            if (args.SequenceEqual(new[] { "--verify-restart" }))
            {
                await Case("pg_new_process_receipt_recovery", async () =>
                {
                    var proof = JsonSerializer.Deserialize<Restart>(await File.ReadAllTextAsync(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_RESTART_PROOF")!))!;
                    var result = await Game().RegisterMountAsync(proof.Account, proof.CharacterId, proof.Command, Catalog(8), default);
                    Check(result.Replayed && JsonSerializer.Serialize(result.Receipt) == JsonSerializer.Serialize(proof.Receipt), "Fresh process and connection recover exact receipt, no process cache");
                    Check((await Game().GetRidingCollectionAsync(proof.Account, proof.CharacterId, default)).Revision == 1, "Unlock persisted across process restart");
                });
            }
            else if (args.Length == 0) { await Game().EnsureCreatedAsync(default); await MainCases(); }
            else throw new ArgumentException("Unknown test mode");
            Console.WriteLine(JsonSerializer.Serialize(new { Passed = true, CaseCount = cases.Count, Assertions = assertions, Cases = cases })); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e.GetType().Name + ": " + e.Message); Console.WriteLine(JsonSerializer.Serialize(new { Passed = false, Assertions = assertions, Cases = cases })); return 1; }
    }
}
