using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class ReservationCases
{
    private static int checks, sequence;
    private static DateTimeOffset Now => DateTimeOffset.UtcNow;
    private static TradeItemPolicy Policy() => new([new("Material", "Test.Ore", 1, true, true, 999)]);

    public static async Task<int> Run(bool postgres)
    {
        try
        {
            await using Fixture f = await Fixture.Create(postgres);
            await GuardAndRelease(f);
            await AtomicFailures(f);
            await WalletScopes(f);
            await Concurrency(f);
            await Expiry(f);
            await RewardDelivery(f);
            if (postgres) await PostgresFailuresAndReload(f);
            Console.WriteLine($"PASS Reservations {(postgres ? "PostgreSQL" : "Memory")}: {checks} assertions; no HTTP/UE/settlement claim.");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("FAIL Reservations: " + (ex is PostgresException pg ? "SQLSTATE " + pg.SqlState : ex.Message));
            Console.Error.WriteLine(ex.StackTrace);
            return 1;
        }
    }

    private static TradeReservationCommand Command(TradeParticipant p, ItemRecord? item, string code = "RosterGold", long amount = 700) =>
        new(Guid.NewGuid(), Guid.NewGuid(), 1, p, code,
            new(item is null ? [] : [new(item.ItemId, item.Revision, 30)], amount), Now.AddMinutes(3));
    private static ItemRepositoryCommitRequest Mutation(ItemRecord item, int quantity) =>
        new(Guid.NewGuid(), "Test.Change", "change", item.Owner, 1,
            [new(item.Revision, item with { State = item.State with { Quantity = quantity } })]);
    private static CurrencyTransactionRequest Money(TradeParticipant p, string code, long delta) =>
        new(Guid.NewGuid(), p.CharacterId, Guid.NewGuid(), "Test.Money", Guid.NewGuid().ToString("N"), "Test", [new(code, delta)]);

    private static async Task GuardAndRelease(Fixture f)
    {
        var p = await f.Character(); var item = await f.Item(p); await f.Fund(p);
        var command = Command(p, item); var result = await f.Store.ReserveAsync(command, default);
        Check(result.Status == TradeReservationStatus.Active, "Reservation created.");
        Check((await f.Items.FindAsync(item.ItemId, default))!.Revision == item.Revision && await f.Balance(p) == 1000, "Reserving does not move or deduct assets.");
        Check((await f.Items.CommitAsync(Mutation(item, 99), Now.AddYears(5), default)).Status == ItemRepositoryCommitStatus.ValidationFailed, "Future commit timestamp cannot bypass item hold.");
        var spoof = Mutation(item, 99) with { Operation = "Trade.Settle", CommandFingerprint = command.ReservationId.ToString() };
        Check((await f.Items.CommitAsync(spoof, Now, default)).Status == ItemRepositoryCommitStatus.ValidationFailed, "Operation name is not a reservation bypass.");
        Check((await f.Spend(p, -301)).Status == CurrencyTransactionStatus.InsufficientBalance, "Reserved money cannot be spent.");
        var spend = Money(p, "RosterGold", -300);
        Check((await f.Economy.CommitAsync(spend, await f.Context(p), Now, default)).Status == CurrencyTransactionStatus.Committed, "Unreserved balance remains usable.");
        Check((await f.Economy.CommitAsync(spend, await f.Context(p), Now, default)).Status == CurrencyTransactionStatus.AlreadyCommitted && await f.Balance(p) == 700, "Historical receipt replays without charging again.");
        Check((await f.Store.ReserveAsync(command, default)).Fingerprint == result.Fingerprint, "Reservation replay is stable.");
        await Reject(() => f.Store.ReserveAsync(command with { Offer = new(command.Offer.Items, 699) }, default), "reservation_idempotency_conflict");
        var foreign = await f.Character();
        Check(await f.Store.FindAsync(command.ReservationId, foreign, default) is null, "Foreign reservation lookup hidden.");
        await Reject(() => f.Store.ReleaseAsync(command with { Participant = foreign }, default), "reservation_idempotency_conflict");
        await Reject(() => f.Store.ReserveAsync(command with { ReservationId = Guid.NewGuid(), Participant = p with { AccountId = foreign.AccountId } }, default), "character_not_owned");
        await f.Fund(foreign);
        await Reject(() => f.Store.ReserveAsync(command with { ReservationId = Guid.NewGuid(), Participant = foreign }, default), "reservation_item_not_found");
        Check((await f.Store.ReleaseAsync(command, default)).Status == TradeReservationStatus.Released, "Release succeeds.");
        Check((await f.Store.ReserveAsync(command, default)).Status == TradeReservationStatus.Released, "Released ID cannot reactivate.");
        Check((await f.Items.CommitAsync(Mutation(item, 99), Now, default)).Status == ItemRepositoryCommitStatus.Committed, "Release restores item use.");
        Check((await f.Spend(p, -700)).Status == CurrencyTransactionStatus.Committed, "Release restores money use.");
        var delayed = Command(p, null, amount: 0);
        await f.Store.ReleaseAsync(delayed, default);
        Check((await f.Store.ReserveAsync(delayed, default)).Status == TradeReservationStatus.Released, "Cancel-before-create seals delayed reserve.");
        Console.WriteLine("PASS holds, normal commits, exact replay, ownership and cancellation tombstones");
    }

    private static async Task AtomicFailures(Fixture f)
    {
        var p = await f.Character(); var item = await f.Item(p); var second = await f.Item(p, 71); await f.Fund(p);
        var invalid = Command(p, item) with { Offer = new([new(item.ItemId, item.Revision, 30), new(second.ItemId, second.Revision + 1, 10)], 700) };
        await Reject(() => f.Store.ReserveAsync(invalid, default), "item_revision_conflict");
        Check(await f.Store.FindAsync(invalid.ReservationId, p, default) is null, "A later item failure leaves no reservation.");
        var command = Command(p, item); await f.Store.ReserveAsync(command, default);
        var firstChange = Mutation(second, 90);
        var both = firstChange with { Mutations = [.. firstChange.Mutations, new(item.Revision, item with { State = item.State with { Quantity = 90 } })] };
        Check((await f.Items.CommitAsync(both, Now, default)).Status == ItemRepositoryCommitStatus.ValidationFailed, "Mixed item batch fails wholly.");
        Check((await f.Items.FindAsync(second.ItemId, default))!.State.Quantity == 100
            && await f.Items.TryGetCommitResultAsync(both.RequestId, default) is null, "No earlier item update or receipt survives.");
        var context = await f.Context(p);
        var credit = Money(p, "RosterGold", 50) with { RequestId = Guid.Parse("00000001-0000-4000-8000-000000000001") };
        var debit = Money(p, "RosterGold", -351) with { RequestId = Guid.Parse("00000002-0000-4000-8000-000000000002") };
        var batch = await f.Economy.CommitBatchAsync([new(credit, context), new(debit, context)], Now, default);
        Check(batch.Any(value => value.Status == CurrencyTransactionStatus.InsufficientBalance) && await f.Balance(p) == 1000,
            "Later reserved-money failure rolls back earlier credit.");
        Check(await f.Economy.TryGetTransactionResultAsync(credit.RequestId, default) is null, "Failed batch has no partial receipt.");
        await f.Store.ReleaseAsync(command, default);
        Console.WriteLine("PASS all-or-nothing multi-item/multi-currency failures");
    }

    private static async Task WalletScopes(Fixture f)
    {
        foreach (var scope in new[] { CurrencyScope.Account, CurrencyScope.Roster, CurrencyScope.Character })
        {
            string code = scope + "Test";
            await f.Economy.UpsertDefinitionAsync(new(code, code, scope, 1000000, true), Now, default);
            var p = await f.Character(); var alt = await f.Character(p.AccountId);
            await f.Fund(p, code);
            if (scope == CurrencyScope.Character) await f.Fund(alt, code);
            var hold = Command(p, null, code); await f.Store.ReserveAsync(hold, default);
            var other = Command(alt, null, code, 400);
            if (scope == CurrencyScope.Character)
            {
                Check((await f.Store.ReserveAsync(other, default)).Status == TradeReservationStatus.Active, "Separate character wallets reserve independently.");
                await f.Store.ReleaseAsync(other, default);
            }
            else
            {
                await Reject(() => f.Store.ReserveAsync(other, default), "reservation_insufficient_balance");
                Check((await f.Spend(alt, -301, code)).Status == CurrencyTransactionStatus.InsufficientBalance, "Alt cannot spend shared held balance.");
            }
            await f.Store.ReleaseAsync(hold, default);
        }
        Console.WriteLine("PASS account/roster/character wallet ownership boundaries");
    }

    private static async Task Concurrency(Fixture f)
    {
        var p = await f.Character(); await f.Fund(p); var item = await f.Item(p);
        var same = Command(p, item);
        var duplicates = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() => f.Store.ReserveAsync(same, default))));
        Check(duplicates.All(value => value.Fingerprint == duplicates[0].Fingerprint), "Concurrent identical requests have one identity.");
        await f.Store.ReleaseAsync(same, default);
        var one = Command(p, item); var two = Command(p, item);
        async Task<bool> Attempt(TradeReservationCommand command)
        {
            try { await f.Store.ReserveAsync(command, default); return true; }
            catch (TradeRuleException ex) when (ex.Code is "character_already_reserved" or "reservation_insufficient_balance") { return false; }
        }
        var winners = await Task.WhenAll(Task.Run(() => Attempt(one)), Task.Run(() => Attempt(two)));
        Check(winners.Count(value => value) == 1, "Conflicting reservations have one winner.");
        await f.Store.ReleaseAsync(winners[0] ? one : two, default);
        var race = Command(p, null);
        var reserveTask = Task.Run(() => Attempt(race));
        var spendTask = Task.Run(() => f.Spend(p, -400));
        await Task.WhenAll(reserveTask, spendTask);
        bool reserveWon = await reserveTask;
        var spent = await spendTask;
        Check(reserveWon != (spent.Status == CurrencyTransactionStatus.Committed), "Reserve/spend race cannot oversubscribe balance.");
        await f.Store.ReleaseAsync(race, default);
        var cancelRace = Command(p, item, amount: 0);
        await Task.WhenAll(Task.Run(() => f.Store.ReserveAsync(cancelRace, default)), Task.Run(() => f.Store.ReleaseAsync(cancelRace, default)));
        Check((await f.Store.ReserveAsync(cancelRace, default)).Status == TradeReservationStatus.Released, "Concurrent cancel wins permanently over delayed retries.");
        var itemRace = Command(p, item, amount: 0);
        async Task<bool> ReserveItem()
        {
            try { await f.Store.ReserveAsync(itemRace, default); return true; }
            catch (TradeRuleException ex) when (ex.Code == "item_revision_conflict") { return false; }
        }
        var itemReserveTask = Task.Run(ReserveItem);
        var mutationTask = Task.Run(() => f.Items.CommitAsync(Mutation(item, 99), Now, default));
        await Task.WhenAll(itemReserveTask, mutationTask).WaitAsync(TimeSpan.FromSeconds(15));
        Check(await itemReserveTask != ((await mutationTask).Status == ItemRepositoryCommitStatus.Committed),
            "Item revision change and reservation cannot both succeed against the same snapshot.");
        await f.Store.ReleaseAsync(itemRace, default);
        Console.WriteLine("PASS duplicate/conflicting reservation, spend, item mutation and cancel races");
    }

    private static async Task Expiry(Fixture f)
    {
        var p = await f.Character(); await f.Fund(p); var item = await f.Item(p);
        var command = Command(p, item) with { ExpiresAt = Now.AddSeconds(1.5) };
        await f.Store.ReserveAsync(command, default);
        Check((await f.Spend(p, -301, committedAt: Now.AddYears(5))).Status == CurrencyTransactionStatus.InsufficientBalance,
            "Caller timestamp cannot prematurely expire money hold.");
        await Task.Delay(TimeSpan.FromSeconds(1.7));
        Check((await f.Store.FindAsync(command.ReservationId, p, default))!.Status == TradeReservationStatus.Expired, "Expired status survives query without a cleanup worker.");
        Check((await f.Store.ReserveAsync(command, default)).Status == TradeReservationStatus.Expired, "Expired receipt cannot reacquire.");
        Check((await f.Items.CommitAsync(Mutation(item, 99), Now, default)).Status == ItemRepositoryCommitStatus.Committed,
            "Expired item hold stops blocking automatically.");
        Check((await f.Spend(p, -1000)).Status == CurrencyTransactionStatus.Committed, "Expired money hold stops blocking automatically.");
        Console.WriteLine("PASS server-clock expiry and stale command timestamp refusal");
    }

    private static async Task RewardDelivery(Fixture f)
    {
        var p = await f.Character(); await f.Fund(p); var item = await f.Item(p);
        var command = Command(p, item); await f.Store.ReserveAsync(command, default);
        var rewardItem = item with { Revision = 0, State = item.State with { InstanceId = Guid.NewGuid(), Quantity = 5 },
            Location = new(ItemContainerType.Mail, "TestReward", 0) };
        var request = new ItemRepositoryCommitRequest(Guid.NewGuid(), "DungeonReward", "TestReward", item.Owner, 5, [new(0, rewardItem)]);
        var credit = Money(p, "RosterGold", 10);
        var batch = new DungeonRewardCommitBatch([new(credit, await f.Context(p))], [request], [999]);
        Check((await f.Rewards.CommitAsync(batch, Now, default)).Succeeded, "Rewards can arrive while a stack is reserved.");
        Check((await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 100, "Reserved stack not selected for reward merge.");
        var delivered = await f.Items.FindAsync(rewardItem.ItemId, default);
        Check(delivered?.Location.ContainerType == ItemContainerType.Inventory && delivered.Location.SlotIndex != item.Location.SlotIndex,
            "Occupied reserved slot remains occupied; reward goes to another slot.");
        Check(await f.Balance(p) == 1010, "Credit remains possible during a hold.");
        Check((await f.Rewards.CommitAsync(batch, Now, default)).Succeeded && await f.Balance(p) == 1010, "Reward replay does not redeliver.");
        // Fill the bag and protect the new reward stack; only the reserved stack could otherwise merge.
        var protect = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Test.Lock", "lock", item.Owner, 0,
            [new(delivered!.Revision, delivered with { Metadata = delivered.Metadata with { IsLocked = true } })]);
        Check((await f.Items.CommitAsync(protect, Now, default)).Status == ItemRepositoryCommitStatus.Committed, "Fixture user lock.");
        var inventory = await f.Items.FindByOwnerAsync(item.Owner, false, 500, default);
        var occupied = inventory.Where(value => value.Location.ContainerType == ItemContainerType.Inventory).Select(value => value.Location.SlotIndex).ToHashSet();
        foreach (int[] slots in Enumerable.Range(0, 80).Where(slot => !occupied.Contains(slot)).Chunk(16))
        {
            var mutations = slots.Select(slot => new ItemRecordMutation(0, item with { Revision = 0, DefinitionName = "Test.Blocker",
                State = item.State with { InstanceId = Guid.NewGuid(), Quantity = 1 }, Location = item.Location with { SlotIndex = slot } })).ToArray();
            var fill = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Test.Fill", "fill", item.Owner, slots.Length, mutations);
            Check((await f.Items.CommitAsync(fill, Now, default)).Status == ItemRepositoryCommitStatus.Committed, "Fill remaining inventory slots.");
        }
        var overflowItem = rewardItem with { State = rewardItem.State with { InstanceId = Guid.NewGuid() }, Location = new(ItemContainerType.Mail, "Overflow", 0) };
        var overflow = request with { RequestId = Guid.NewGuid(), CommandFingerprint = "Overflow", Mutations = [new(0, overflowItem)] };
        Check((await f.Rewards.CommitAsync(new([], [overflow], [999]), Now, default)).Succeeded, "Full bag with a reserved stack still accepts reward.");
        Check((await f.Items.FindAsync(overflowItem.ItemId, default))!.Location.ContainerType == ItemContainerType.Mail
            && (await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 100, "Reserved slot counts toward capacity; excess goes to mail without merging.");
        await f.Store.ReleaseAsync(command, default);
        Console.WriteLine("PASS inventory-first reward delivery, held-stack exclusion, full-bag mail and replay");
    }

    private static async Task PostgresFailuresAndReload(Fixture f)
    {
        var p = await f.Character(); await f.Fund(p); var item = await f.Item(p); var command = Command(p, item);
        await using var conn = await f.Data!.OpenConnectionAsync();
        await using (var inject = new NpgsqlCommand("""
            CREATE FUNCTION trade_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'fixture'; END $$;
            CREATE TRIGGER trade_test_fail BEFORE INSERT ON trade_reserved_items FOR EACH ROW EXECUTE FUNCTION trade_test_fail();
            """, conn)) await inject.ExecuteNonQueryAsync();
        try
        {
            try { await f.Store.ReserveAsync(command, default); throw new Exception("Injected write unexpectedly succeeded."); }
            catch (PostgresException ex) when (ex.SqlState == "P0001") { checks++; }
        }
        finally
        {
            await using var cleanup = new NpgsqlCommand("DROP TRIGGER trade_test_fail ON trade_reserved_items; DROP FUNCTION trade_test_fail();", conn);
            await cleanup.ExecuteNonQueryAsync();
        }
        Check(await f.Store.FindAsync(command.ReservationId, p, default) is null && await f.Balance(p) == 1000,
            "Failure after reservation header insert rolls back every write.");
        var held = await f.Store.ReserveAsync(command, default);
        await using var reopened = NpgsqlDataSource.Create(f.Connection!);
        var fresh = new PostgresTradeReservationRepository(reopened, Policy());
        Check((await fresh.FindAsync(command.ReservationId, p, default))!.Fingerprint == held.Fingerprint, "Fresh pool reconstructs persistent reservation.");
        Check((await fresh.ReserveAsync(command, default)).Status == TradeReservationStatus.Active, "Fresh store replays same request.");
        await fresh.ReleaseAsync(command, default);
        Check((await f.Store.ReserveAsync(command, default)).Status == TradeReservationStatus.Released, "Release is visible to an earlier store instance.");
        // A command's deadline must be checked after waiting for a real database wallet lock.
        await using var transaction = await conn.BeginTransactionAsync();
        var context = await f.Context(p);
        await using (var holdWallet = new NpgsqlCommand("SELECT balance FROM currency_balances WHERE owner_type='Roster' AND owner_id=@id AND currency_code='RosterGold' FOR UPDATE;", conn, transaction))
        {
            holdWallet.Parameters.AddWithValue("id", context.RosterId.ToString("D"));
            await holdWallet.ExecuteScalarAsync();
        }
        var late = Command(p, item) with { ExpiresAt = Now.AddSeconds(1) };
        var pending = f.Store.ReserveAsync(late, default);
        await Task.Delay(1300);
        await transaction.CommitAsync();
        await Reject(() => pending, "reservation_expired");
        Check(await f.Store.FindAsync(late.ReservationId, p, default) is null, "Expiry during lock wait leaves no hold.");
        Console.WriteLine("PASS PostgreSQL mid-write rollback, fresh-pool replay and real lock-wait expiry");
    }

    private static void Check(bool value, string message) { if (!value) throw new Exception(message); checks++; }
    private static async Task Reject(Func<Task> action, string code)
    {
        try { await action(); }
        catch (TradeRuleException ex) when (ex.Code == code) { checks++; return; }
        throw new Exception("Expected rejection: " + code);
    }

    private sealed class Fixture : IAsyncDisposable
    {
        public NpgsqlDataSource? Data;
        public string? Connection;
        public required IGameRepository Game;
        public required IItemRepository Items;
        public required IEconomyRepository Economy;
        public required ITradeReservationRepository Store;
        public required IDungeonRewardCommitRepository Rewards;

        public static async Task<Fixture> Create(bool postgres)
        {
            Fixture f;
            if (!postgres)
            {
                InMemoryTransactionGate gate = new(); InMemoryItemRepository items = new(gate);
                InMemoryEconomyRepository economy = new(gate); InMemoryGameRepository game = new(gate, items);
                f = new() { Game = game, Items = items, Economy = economy,
                    Store = new InMemoryTradeReservationRepository(gate, items, economy, game, Policy()),
                    Rewards = new InMemoryDungeonRewardCommitRepository(gate, economy, items) };
                try { _ = new InMemoryTradeReservationRepository(new(), items, economy, game, Policy()); throw new Exception("Mismatched gates accepted."); }
                catch (InvalidOperationException) { checks++; }
            }
            else
            {
                if (!Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_RUN"), "N", out _)) throw new Exception("Missing private run identity.");
                NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_POSTGRES"));
                if (config.Host != "127.0.0.1" || config.Database != "trade_test") throw new Exception("Refusing non-test database.");
                var data = NpgsqlDataSource.Create(config.ConnectionString);
                PostgresItemRepository items = new(data, NullLogger<PostgresItemRepository>.Instance);
                PostgresEconomyRepository economy = new(data);
                PostgresGameRepository game = new(data, NullLogger<PostgresGameRepository>.Instance, equipmentItems: items);
                f = new() { Data = data, Connection = config.ConnectionString, Game = game, Items = items, Economy = economy,
                    Store = new PostgresTradeReservationRepository(data, Policy()),
                    Rewards = new PostgresDungeonRewardCommitRepository(data, economy, items, NullLogger<PostgresDungeonRewardCommitRepository>.Instance) };
                var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog.Take(7).ToArray(), default) == 7, "Create old v7 database.");
                var old = await f.Character();
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog, default) == 1, "Only v8 applies to v7.");
                Check(await PostgresSchemaMigrations.ApplyAsync(data, catalog, default) == 0, "Migration replay is empty.");
                Check((await f.Context(old)).SteamId == old.AccountId, "Migration preserves existing character.");
            }
            await f.Economy.UpsertDefinitionAsync(new("RosterGold", "Gold", CurrencyScope.Roster, 1000000, true), Now, default);
            return f;
        }

        public async Task<TradeParticipant> Character(string? account = null)
        {
            int id = Interlocked.Increment(ref sequence);
            account ??= (76561198100000000L + id).ToString();
            await Game.UpsertAccountAsync(new(account, account, false, false), Now, default);
            var character = await Game.CreateCharacterAsync(account, "Trade" + id, Now, default);
            return new(account, character.CharacterId);
        }
        public async Task<CharacterEconomyContext> Context(TradeParticipant p) => (await Game.GetCharacterEconomyContextAsync(p.CharacterId, default))!;
        public async Task<ItemRecord> Item(TradeParticipant p, int slot = 70)
        {
            ItemOwnerRef owner = new(ItemOwnerType.Character, p.CharacterId.ToString("D"));
            ItemRecord item = new("Material", "Test.Ore", 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, slot),
                new(Guid.NewGuid(), 1, 100, [], []), 0, ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, "Test", false));
            var result = await Items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", Guid.NewGuid().ToString("N"), owner, 100, [new(0, item)]), Now, default);
            Check(result.Status == ItemRepositoryCommitStatus.Committed, "Fixture item creation.");
            return result.Records.Single();
        }
        public async Task Fund(TradeParticipant p, string code = "RosterGold") => Check((await Spend(p, 1000, code)).Status == CurrencyTransactionStatus.Committed, "Fixture funding.");
        public async Task<CurrencyTransactionResult> Spend(TradeParticipant p, long delta, string code = "RosterGold", DateTimeOffset? committedAt = null) =>
            await Economy.CommitAsync(Money(p, code, delta), await Context(p), committedAt ?? Now, default);
        public async Task<long> Balance(TradeParticipant p, string code = "RosterGold") =>
            (await Economy.LoadWalletAsync(await Context(p), default)).Balances.Single(value => value.Definition.CurrencyCode == code).Balance;
        public async ValueTask DisposeAsync() { if (Data is not null) await Data.DisposeAsync(); }
    }
}
