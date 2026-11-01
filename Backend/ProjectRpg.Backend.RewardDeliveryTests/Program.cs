using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Authentication;
using static TestClock;

Guid scopedSession = Guid.NewGuid();
Guid scopedCharacter = Guid.NewGuid();
var restrictedServer = new AuthenticatedPrincipal(PrincipalKind.GameServer, null, "reward-test", scopedSession, true);
string snapshotPath = $"/api/dungeon-sessions/{scopedSession:D}/reward-snapshot/{scopedCharacter:D}";
Assert(DungeonRewardSnapshotAccess.IsOwnReadOnlyRoute("GET", snapshotPath, restrictedServer));
foreach (string method in new[] { "POST", "PUT", "DELETE", "PATCH", "HEAD" })
    Assert(!DungeonRewardSnapshotAccess.IsOwnReadOnlyRoute(method, snapshotPath, restrictedServer));
foreach (string path in new[] { snapshotPath + "/extra", snapshotPath + "/", "/api/items", "/api/security-extra",
    $"/api/economy/wallets/{scopedCharacter:D}", snapshotPath.Replace(scopedSession.ToString("D"), Guid.NewGuid().ToString("D")),
    snapshotPath.Replace(scopedCharacter.ToString("D"), Guid.Empty.ToString("D")) })
    Assert(!DungeonRewardSnapshotAccess.IsOwnReadOnlyRoute("GET", path, restrictedServer));
Assert(!DungeonRewardSnapshotAccess.IsOwnReadOnlyRoute("GET", snapshotPath, restrictedServer with { DungeonSessionId = null }));
Assert(!DungeonRewardSnapshotAccess.IsOwnReadOnlyRoute("GET", snapshotPath, restrictedServer with { Kind = PrincipalKind.Player }));
Console.WriteLine("PASS Authorization: only GET for the credential-bound reward snapshot is an exception.");

await TestMemorySettlementCredential();

await RunSuite("InMemory", CreateMemoryStore);
string? connectionString = Environment.GetEnvironmentVariable("PROJECT_RPG_REWARD_TEST_POSTGRES");
if (!string.IsNullOrWhiteSpace(connectionString))
{
    await using NpgsqlDataSource source = NpgsqlDataSource.Create(connectionString);
    await PostgresSchemaInitializer.ApplyAsync(source,
        await PostgresSchemaMigrations.LoadInitializationSqlAsync(AppContext.BaseDirectory, CancellationToken.None),
        CancellationToken.None);
    await RunSuite("Postgres", () => CreatePostgresStore(source));
}
else Console.WriteLine("Postgres suite skipped (set PROJECT_RPG_REWARD_TEST_POSTGRES for the local test database).");

static async Task RunSuite(string name, Func<Task<Store>> create)
{
    int passed = 0;
    async Task Test(string label, Func<Store, Task> body)
    {
        Store store = await create();
        await body(store);
        Console.WriteLine($"PASS {name}: {label}");
        passed++;
    }

    await Test("empty inventory receives items and currency atomically", async store =>
    {
        var grant = Grant(store.Owner, ("Potion", 3), ("Helm", 1));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10, 1]), Now, default)).Succeeded);
        var records = await store.Items.FindByOwnerAsync(store.Owner, false, 1000, default);
        Assert(records.Count == 2 && records.All(record => record.Location.ContainerType == ItemContainerType.Inventory));
        Assert(records.Select(record => record.Location.SlotIndex).Order().SequenceEqual([0, 1]));
        Assert(records.All(record => record.Revision == 1));
        Assert(grant.Mutations.All(mutation => records.Any(record => record.ItemId == mutation.NewRecord.ItemId)));
        Assert(await store.Balance() == 100);
    });

    await Test("existing stack first, then lowest empty slot", async store =>
    {
        ItemRecord stack = Item(store.Owner, "Potion", 7, 3);
        await Seed(store.Items, [stack]);
        var grant = Grant(store.Owner, ("Potion", 5));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(stack.ItemId, default)) is { State.Quantity: 10, Revision: 2 });
        ItemRecord? remainder = await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default);
        Assert(remainder is { State.Quantity: 2, Location.ContainerType: ItemContainerType.Inventory, Location.SlotIndex: 0 });
    });

    await Test("full bag still fills stacks; only remainder becomes mail", async store =>
    {
        ItemRecord stack = Item(store.Owner, "Potion", 8, 0);
        await Seed(store.Items, [stack, .. Fill(store.Owner, 1, 79)]);
        var grant = Grant(store.Owner, ("Potion", 5));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(stack.ItemId, default))?.State.Quantity == 10);
        Assert((await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default))
            is { State.Quantity: 3, Location.ContainerType: ItemContainerType.Mail, Revision: 1 });
    });

    await Test("full bag without stack space keeps entire reward in mail", async store =>
    {
        await Seed(store.Items, Fill(store.Owner, 0, 80));
        var grant = Grant(store.Owner, ("Potion", 5));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default)).Succeeded);
        ItemRecord? actual = await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default);
        Assert(actual is { State.Quantity: 5, Location.ContainerType: ItemContainerType.Mail });
        Assert(actual!.Location == grant.Mutations[0].NewRecord.Location);
    });

    await Test("one free slot: first reward inventory, second reward mail", async store =>
    {
        await Seed(store.Items, Fill(store.Owner, 0, 79));
        var grant = Grant(store.Owner, ("Potion", 3), ("Helm", 1));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10, 1]), Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default))
            is { Location.ContainerType: ItemContainerType.Inventory, Location.SlotIndex: 79 });
        Assert((await store.Items.FindAsync(grant.Mutations[1].NewRecord.ItemId, default))
            is { Location.ContainerType: ItemContainerType.Mail });
    });

    await Test("multiple reward lines aggregate one mutation per stack", async store =>
    {
        ItemRecord stack = Item(store.Owner, "Potion", 9, 0);
        await Seed(store.Items, [stack]);
        var grant = Grant(store.Owner, ("Potion", 3), ("Potion", 4));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10, 10]), Now, default)).Succeeded);
        var records = await store.Items.FindByOwnerAsync(store.Owner, false, 1000, default);
        Assert(records.Count == 2 && records.Sum(record => record.State.Quantity) == 16);
        Assert(records.Single(record => record.ItemId == stack.ItemId).Revision == 2);
        Assert(records.Single(record => record.ItemId != stack.ItemId) is { State.Quantity: 6, Revision: 1 });
    });

    await Test("metadata ordering does not prevent a compatible stack merge", async store =>
    {
        ItemRecord stack = Item(store.Owner, "Potion", 8, 0) with
        {
            State = Item(store.Owner, "Potion", 8, 0).State with
            {
                InstanceTags = ["Item.B", "Item.A"],
                StatValues = [new("Stat.B", 2), new("Stat.A", 1)]
            }
        };
        await Seed(store.Items, [stack]);
        var grant = Grant(store.Owner, ("Potion", 2));
        var reward = grant.Mutations[0].NewRecord;
        reward = reward with { State = reward.State with
        {
            InstanceTags = ["Item.A", "Item.B"], StatValues = [new("Stat.A", 1), new("Stat.B", 2)]
        } };
        grant = grant with { Mutations = [new(0, reward)] };
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(stack.ItemId, default)) is { State.Quantity: 10, Revision: 2 });
        Assert(await store.Items.FindAsync(reward.ItemId, default) is null);
    });

    await Test("inventory scan is not truncated by a large existing mailbox", async store =>
    {
        ItemRecord stack = Item(store.Owner, "Potion", 8, 79);
        var mail = Enumerable.Range(0, 510).Select(slot => Item(store.Owner, "OldMail", 1, slot) with
        {
            Location = new(ItemContainerType.Mail, "OldMailbox", slot)
        }).ToArray();
        await Seed(store.Items, [stack, .. mail]);
        var grant = Grant(store.Owner, ("Potion", 2));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(stack.ItemId, default)) is { State.Quantity: 10, Revision: 2 });
        Assert(await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default) is null);
    });

    await Test("deterministic grant ID collision is not hidden by a full merge", async store =>
    {
        var grant = Grant(store.Owner, ("Potion", 2));
        ItemRecord collision = grant.Mutations[0].NewRecord with
        {
            State = grant.Mutations[0].NewRecord.State with { Quantity = 8 },
            Location = new(ItemContainerType.Inventory, store.Owner.OwnerId, 0)
        };
        await Seed(store.Items, [collision]);
        Assert(!(await store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(collision.ItemId, default)) is { State.Quantity: 8, Revision: 1 });
        Assert(await store.Balance() == 0);
    });

    await Test("invalid stack limits rejected by settlement rules", store =>
    {
        var reward = new DungeonItemReward("TestItem", "Potion", 1, 3, ItemBindState.Unbound, 0, 0, [], []);
        Assert(!DungeonRewardRules.TryValidateItemReward(reward with { MaxStackSize = -1 }, out _));
        Assert(!DungeonRewardRules.TryValidateItemReward(reward with { MaxStackSize = 2 }, out _));
        Assert(DungeonRewardRules.TryValidateItemReward(reward with { MaxStackSize = 3 }, out _));
        return Task.CompletedTask;
    });

    var incompatible = new (string Label, Func<ItemRecord, ItemRecord> Change)[]
    {
        ("definition version", record => record with { DefinitionVersion = 2 }),
        ("tags", record => record with { State = record.State with { InstanceTags = ["Item.Special"] } }),
        ("stats", record => record with { State = record.State with { StatValues = [new("Stat.Attack", 3)] } }),
        ("bind state", record => record with { Metadata = record.Metadata with { BindState = ItemBindState.CharacterBound } }),
        ("durability", record => record with { Metadata = record.Metadata with { Durability = new(4, 10) } }),
        ("locked", record => record with { Metadata = record.Metadata with { IsLocked = true } }),
        ("expiry", record => record with { Metadata = record.Metadata with { ExpiresAtUtc = Now.AddDays(-1) } })
    };
    foreach (var (label, change) in incompatible)
    {
        await Test($"does not merge incompatible {label}", async store =>
        {
            ItemRecord stack = change(Item(store.Owner, "Potion", 8, 0));
            await Seed(store.Items, [stack]);
            var grant = Grant(store.Owner, ("Potion", 2));
            Assert((await store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default)).Succeeded);
            Assert((await store.Items.FindAsync(stack.ItemId, default)) is { State.Quantity: 8, Revision: 1 });
            Assert((await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default)) is { State.Quantity: 2, Location.SlotIndex: 1 });
        });
    }

    await Test("large internal grant conserves quantity across all inventory stacks", async store =>
    {
        var stacks = Enumerable.Range(0, 80).Select(slot => Item(store.Owner, "Potion", 9, slot)).ToArray();
        await Seed(store.Items, stacks);
        var oversizedPublic = new ItemRepositoryCommitRequest(Guid.NewGuid(), "AdminSeed", "too-large", store.Owner, 0,
            stacks.Select(record => new ItemRecordMutation(1, record with { Revision = 1 })).ToArray());
        Assert((await store.Items.CommitAsync(oversizedPublic, Now, default)).Status == ItemRepositoryCommitStatus.InvalidRequest);
        var grant = Grant(store.Owner, Enumerable.Range(0, 8).Select(_ => ("Potion", 10)).ToArray());
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, Enumerable.Repeat(10, 8).ToArray()), Now, default)).Succeeded);
        var records = await store.Items.FindByOwnerAsync(store.Owner, false, 1000, default);
        Assert(records.Count == 80 && records.All(record => record is { State.Quantity: 10, Revision: 2 }));
    });

    await Test("replay after consumption does not pay or plan again", async store =>
    {
        var grant = Grant(store.Owner, ("Potion", 3));
        var batch = store.Batch(grant, [10]);
        Assert((await store.Rewards.CommitAsync(batch, Now, default)).Succeeded);
        ItemRecord record = (await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default))!;
        ItemRecord consumed = record with
        {
            LifecycleState = ItemLifecycleState.Consumed,
            Location = new(ItemContainerType.Terminal, "", -1),
            State = record.State with { Quantity = 0 }
        };
        await Update(store.Items, consumed);
        Assert((await store.Rewards.CommitAsync(batch, Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(record.ItemId, default)) is { State.Quantity: 0, Revision: 2 });
        Assert((await store.Items.FindByOwnerAsync(store.Owner, false, 1000, default)).Count == 0);
        Assert(await store.Balance() == 100);
    });

    await Test("reused request ID with different fingerprint is rejected", async store =>
    {
        var grant = Grant(store.Owner, ("Potion", 3));
        var batch = store.Batch(grant, [10]);
        Assert((await store.Rewards.CommitAsync(batch, Now, default)).Succeeded);
        Assert(!(await store.Rewards.CommitAsync(batch with
        {
            ItemRequests = [grant with { CommandFingerprint = "changed" }]
        }, Now, default)).Succeeded);
        Assert(await store.Balance() == 100);
    });

    await Test("legacy payload remains mail-only and retains old JSON fingerprint", async store =>
    {
        var grant = Grant(store.Owner, ("Potion", 3));
        Assert((await store.Rewards.CommitAsync(store.Batch(grant, [0]), Now, default)).Succeeded);
        Assert((await store.Items.FindAsync(grant.Mutations[0].NewRecord.ItemId, default))?.Location.ContainerType == ItemContainerType.Mail);
        var reward = new DungeonItemReward("TestItem", "Potion", 1, 3, ItemBindState.Unbound, 0, 0, [], []);
        Assert(!JsonSerializer.Serialize(reward).Contains("MaxStackSize", StringComparison.Ordinal));
        Assert(JsonSerializer.Deserialize<DungeonItemReward>(JsonSerializer.Serialize(reward))?.MaxStackSize == 0);
    });

    await Test("party item failure rolls back earlier item, currency and receipts", async store =>
    {
        Store other = await create();
        var first = Grant(store.Owner, ("Potion", 3)) with { RequestId = Guid.Parse("11111111-1111-1111-1111-111111111111") };
        var second = Grant(other.Owner, ("Potion", 3)) with { RequestId = Guid.Parse("22222222-2222-2222-2222-222222222222") };
        // Deliberately occupy the second grant's deterministic item ID before settlement.
        ItemRecord collision = second.Mutations[0].NewRecord with
        {
            DefinitionName = "Filler",
            Location = new(ItemContainerType.Inventory, other.Owner.OwnerId, 0)
        };
        await Seed(store.Items, [collision]);
        var batch = store.Batch(first, [10]);
        batch = batch with { CurrencyEntries = [.. batch.CurrencyEntries, .. other.Batch(second, [10]).CurrencyEntries], ItemRequests = [first, second] };
        Assert(!(await store.Rewards.CommitAsync(batch, Now, default)).Succeeded);
        Assert(await store.Items.FindAsync(first.Mutations[0].NewRecord.ItemId, default) is null);
        Assert(await store.Items.TryGetCommitResultAsync(first.RequestId, default) is null);
        Assert(await store.Balance() == 0);
    });

    await Test("party members use their own bag capacity with ordered concurrent locks", async store =>
    {
        Store other = await create();
        await Seed(store.Items, Fill(store.Owner, 0, 80));
        var first = Grant(store.Owner, ("Potion", 5));
        var second = Grant(other.Owner, ("Potion", 5));
        var batch = store.Batch(first, [10]);
        batch = batch with
        {
            CurrencyEntries = [.. batch.CurrencyEntries, .. other.Batch(second, [10]).CurrencyEntries],
            ItemRequests = [first, second]
        };
        var reversed = batch with
        {
            CurrencyEntries = batch.CurrencyEntries.Reverse().ToArray(), ItemRequests = [second, first]
        };
        var results = await Task.WhenAll(Enumerable.Range(0, 6).Select(index => Task.Run(() =>
            store.Rewards.CommitAsync(index % 2 == 0 ? batch : reversed, Now, default))));
        Assert(results.All(result => result.Succeeded));
        Assert((await store.Items.FindAsync(first.Mutations[0].NewRecord.ItemId, default))
            is { State.Quantity: 5, Location.ContainerType: ItemContainerType.Mail, Revision: 1 });
        Assert((await store.Items.FindAsync(second.Mutations[0].NewRecord.ItemId, default))
            is { State.Quantity: 5, Location.ContainerType: ItemContainerType.Inventory, Location.SlotIndex: 0, Revision: 1 });
        Assert(await store.Balance() == 100 && await other.Balance() == 100);
    });

    await Test("simultaneous settlements choose distinct slots and replay once", async store =>
    {
        var grants = Enumerable.Range(0, 12).Select(_ => Grant(store.Owner, ("Helm", 1))).ToArray();
        var batches = grants.Select(grant => store.Batch(grant, [1])).ToArray();
        var results = await Task.WhenAll(batches.Concat(batches).Select(batch =>
            Task.Run(() => store.Rewards.CommitAsync(batch, Now, default))));
        Assert(results.All(result => result.Succeeded));
        var records = await store.Items.FindByOwnerAsync(store.Owner, false, 1000, default);
        Assert(records.Count == 12 && records.Select(record => record.Location.SlotIndex).Distinct().Count() == 12);
        Assert(await store.Balance() == 1200);
    });

    await Test("normal item commit racing reward cannot occupy the same inventory slot", async store =>
    {
        await Seed(store.Items, Fill(store.Owner, 0, 79));
        var grant = Grant(store.Owner, ("Potion", 3));
        ItemRecord normal = Item(store.Owner, "Helm", 1, 79);
        var ordinary = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Pickup", "race", store.Owner, 1, [new(0, normal)]);
        Task<ItemRepositoryCommitResult> ordinaryTask = Task.Run(() => store.Items.CommitAsync(ordinary, Now, default));
        Task<DungeonRewardCommitResult> rewardTask = Task.Run(() => store.Rewards.CommitAsync(store.Batch(grant, [10]), Now, default));
        Assert((await rewardTask).Succeeded);
        var normalResult = await ordinaryTask;
        Assert(normalResult.Status is ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.LocationConflict);
        var records = await store.Items.FindByOwnerAsync(store.Owner, false, 1000, default);
        Assert(records.Count(record => record.Location.ContainerType == ItemContainerType.Inventory) == 80);
        Assert(records.Single(record => record.ItemId == grant.Mutations[0].NewRecord.ItemId).State.Quantity == 3);
    });

    Console.WriteLine($"{name}: {passed} tests passed.");
}

static async Task TestMemorySettlementCredential()
{
    IGameRepository game = new InMemoryGameRepository();
    DateTimeOffset start = Now;
    const string steam = "76561280000000111", server = "credential-test", hash = "test-credential-hash";
    await game.UpsertAccountAsync(new(steam, steam, false, false), start, default);
    GameCharacter character = await game.CreateCharacterAsync(steam, "credential-test", start, default);
    DungeonSession session = await game.CreateDungeonSessionAsync(steam, character.CharacterId, "Dungeon.Test", "Hard", start, start.AddHours(1), default);
    await game.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:7777", start, start.AddHours(1), default);
    await game.StartDungeonSessionAsync(session.DungeonSessionId, server, start, start.AddHours(1), default);
    await game.StoreGameServerCredentialAsync(hash, server, session.DungeonSessionId, start.AddHours(2), default);
    TimeSpan grace = TimeSpan.FromSeconds(300);
    Assert((await game.ResolveGameServerCredentialAsync(hash, start, grace, default)) is { IsSecurityTelemetryOnly: false });
    await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId, server, "test_v1", "test-fingerprint", [new("TestGold", 100)], [], start, default);
    Assert((await game.ResolveGameServerCredentialAsync(hash, start.AddSeconds(1), grace, default)) is { IsSecurityTelemetryOnly: true });
    Assert(await game.ResolveGameServerCredentialAsync(hash, start.AddSeconds(301), grace, default) is null);
    Assert((await game.ClaimNextDungeonRewardSettlementAsync("worker", start.AddSeconds(302), start.AddSeconds(400), default))?.DungeonSessionId == session.DungeonSessionId);
    await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId, "worker", start.AddSeconds(303), default);
    Assert((await game.ResolveGameServerCredentialAsync(hash, start.AddSeconds(304), grace, default)) is { IsSecurityTelemetryOnly: true });
    Assert(await game.ResolveGameServerCredentialAsync(hash, start.AddSeconds(604), grace, default) is null);
    Console.WriteLine("PASS Authorization: pending/complete read-only grace survives pending expiry and ends after completion grace.");
}

static void Assert(bool condition, string message = "Reward delivery assertion failed")
{
    if (!condition) throw new InvalidOperationException(message);
}

static ItemRecord Item(ItemOwnerRef owner, string definition, int quantity, int slot)
    => new("TestItem", definition, 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, slot),
        new(Guid.NewGuid(), 42, quantity, [], []), 0, ItemLifecycleState.Active,
        new(ItemBindState.Unbound, new(0, 0), null, "RewardTest", false));

static ItemRecord[] Fill(ItemOwnerRef owner, int start, int count)
    => Enumerable.Range(start, count).Select(slot => Item(owner, "Filler", 1, slot)).ToArray();

static ItemRepositoryCommitRequest Grant(ItemOwnerRef owner, params (string Definition, int Quantity)[] entries)
{
    Guid session = Guid.NewGuid();
    var mutations = entries.Select((entry, index) => new ItemRecordMutation(0,
        Item(owner, entry.Definition, entry.Quantity, index) with
        {
            Location = new(ItemContainerType.Mail, $"DungeonReward.{session:N}", index)
        })).ToArray();
    return new(Guid.NewGuid(), "DungeonReward", Guid.NewGuid().ToString("N"), owner,
        entries.Sum(entry => entry.Quantity), mutations);
}

static async Task Seed(IItemRepository repository, ItemRecord[] records)
{
    foreach (ItemRecord[] chunk in records.Chunk(ItemRecordRules.MaximumMutationCount))
    {
        var request = new ItemRepositoryCommitRequest(Guid.NewGuid(), "AdminSeed", "seed", chunk[0].Owner,
            chunk.Sum(record => record.State.Quantity), chunk.Select(record => new ItemRecordMutation(0, record)).ToArray());
        Assert((await repository.CommitAsync(request, Now, default)).Status == ItemRepositoryCommitStatus.Committed);
    }
}

static async Task Update(IItemRepository repository, ItemRecord record)
{
    var request = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Consume", "consume", record.Owner, 0,
        [new(record.Revision, record)]);
    Assert((await repository.CommitAsync(request, Now, default)).Status == ItemRepositoryCommitStatus.Committed);
}

static async Task<Store> CreateMemoryStore()
{
    var items = MemoryRepositories.Items;
    var economy = MemoryRepositories.Economy;
    var store = new Store(items, economy, MemoryRepositories.Rewards,
        new(Guid.NewGuid(), Guid.NewGuid(), "reward-test"));
    await store.Initialize();
    return store;
}

static async Task<Store> CreatePostgresStore(NpgsqlDataSource source)
{
    var items = new PostgresItemRepository(source, NullLogger<PostgresItemRepository>.Instance);
    var economy = new PostgresEconomyRepository(source);
    var context = new CharacterEconomyContext(Guid.NewGuid(), Guid.NewGuid(),
        "99" + (BitConverter.ToUInt64(RandomNumberGenerator.GetBytes(8)) % 1_000_000_000_000_000_000UL).ToString("D18"));
    await using (var command = source.CreateCommand("""
        INSERT INTO accounts (steam_id, owner_steam_id, created_at, last_login_at) VALUES (@steam, @steam, NOW(), NOW());
        INSERT INTO rosters (roster_id, steam_id, world_id, created_at, updated_at) VALUES (@roster, @steam, 'main', NOW(), NOW());
        INSERT INTO characters (character_id, roster_id, steam_id, name, created_at, updated_at)
            VALUES (@character, @roster, @steam, @name, NOW(), NOW());
        """))
    {
        command.Parameters.AddWithValue("steam", context.SteamId);
        command.Parameters.AddWithValue("roster", context.RosterId);
        command.Parameters.AddWithValue("character", context.CharacterId);
        command.Parameters.AddWithValue("name", "rt_" + context.CharacterId.ToString("N")[..16]);
        await command.ExecuteNonQueryAsync();
    }
    var store = new Store(items, economy,
        new PostgresDungeonRewardCommitRepository(source, economy, items, NullLogger<PostgresDungeonRewardCommitRepository>.Instance), context);
    await store.Initialize();
    return store;
}

sealed class Store(IItemRepository items, IEconomyRepository economy, IDungeonRewardCommitRepository rewards, CharacterEconomyContext context)
{
    public IItemRepository Items => items;
    public IDungeonRewardCommitRepository Rewards => rewards;
    public ItemOwnerRef Owner { get; } = new(ItemOwnerType.Character, context.CharacterId.ToString("D"));
    private string CurrencyCode { get; } = "RewardTest_" + Guid.NewGuid().ToString("N");

    public Task Initialize() => economy.UpsertDefinitionAsync(new(CurrencyCode, "Test Gold", CurrencyScope.Character, 100000, true), DateTimeOffset.UtcNow, default);

    public DungeonRewardCommitBatch Batch(ItemRepositoryCommitRequest grant, IReadOnlyList<int> maxStackSizes)
        => new([new(new(Guid.NewGuid(), context.CharacterId, Guid.NewGuid(), "dungeon_reward", grant.CommandFingerprint,
            "RewardTest", [new(CurrencyCode, 100)]), context)], [grant], maxStackSizes);

    public async Task<long> Balance() => (await economy.LoadWalletAsync(context, default))
        .Balances.Single(balance => balance.Definition.CurrencyCode == CurrencyCode).Balance;
}

static class TestClock
{
    public static DateTimeOffset Now => DateTimeOffset.UtcNow;
}

static class MemoryRepositories
{
    private static readonly InMemoryTransactionGate Gate = new();
    public static readonly InMemoryItemRepository Items = new(Gate);
    public static readonly InMemoryEconomyRepository Economy = new(Gate);
    public static readonly InMemoryDungeonRewardCommitRepository Rewards = new(Gate, Economy, Items);
}
