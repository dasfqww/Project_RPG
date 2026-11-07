using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

int checks = 0;
void Check(bool value, string label) { if (!value) throw new Exception(label); checks++; }
var now = DateTimeOffset.UtcNow;
ItemOwnerRef Owner() => new(ItemOwnerType.Character, Guid.NewGuid().ToString("D"));
ItemRecord Item(ItemOwnerRef owner, string name, int slot, int quantity = 1) => new("RPGItemDefinition", name, 1,
    owner, new(ItemContainerType.Inventory, owner.OwnerId, slot), new(Guid.NewGuid(), 17, quantity, [], []), 0,
    ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, "Test", false));
ItemRepositoryCommitRequest Command(string operation, ItemOwnerRef owner, IReadOnlyList<ItemRecordMutation> changes) =>
    new(Guid.NewGuid(), operation, Guid.NewGuid().ToString("N"), owner, 0, changes);

async Task Run(IItemRepository repository, string mode)
{
    await using var http = await HttpFixture.Start(repository);
    var owner = Owner();
    var all = Enumerable.Range(0, 240).Select(slot => Item(owner,
        (slot < 80 ? "GameItem.Equipment.Weapon." : slot < 160 ? "GameItem.Consume.Potion." : "GameItem.Material.")
        + (79 - slot % 80).ToString("D3"), slot)).ToArray();
    foreach (var batch in all.Chunk(16))
        Check((await repository.CommitAsync(Command("Test.Seed", owner, batch.Select(i => new ItemRecordMutation(0, i)).ToArray()), now, default)).Status == ItemRepositoryCommitStatus.Committed, mode + " seed");
    var snapshot = await repository.FindByOwnerAsync(owner, false, 500, default);
    Check(snapshot.Count == 240, mode + " all tabs persist");
    for (int tab = 0; tab < 3; tab++)
    {
        var items = snapshot.Where(i => i.Location.SlotIndex / 80 == tab).OrderBy(i => i.DefinitionName, StringComparer.Ordinal).ToArray();
        var request = Command(InventoryBagLayout.SortOperation, owner, items.Select((i, index) =>
            new ItemRecordMutation(i.Revision, i with { Location = i.Location with { SlotIndex = tab * 80 + index } })).ToArray());
        Check(await http.RejectPlayerAndOrdinaryLargeCommit(request), mode + " player denied; ordinary 80-mutation request rejected");
        var sorted = await http.Commit(request);
        Check(sorted.Status == ItemRepositoryCommitStatus.Committed && sorted.Records.Count == 80, mode + " atomic full tab permutation");
        Check(sorted.Records.All(i => i.Revision == 2 && i.State.Quantity == 1), mode + " only location/revision change");
        Check((await http.Commit(request)).Status == ItemRepositoryCommitStatus.AlreadyCommitted, mode + " original receipt replay");
        var tampered = request with { Mutations = request.Mutations.Select(m => m with { NewRecord = m.NewRecord with { State = m.NewRecord.State with { Quantity = 2 } } }).ToArray() };
        Check((await http.Commit(tampered)).Status == ItemRepositoryCommitStatus.IdempotencyConflict, mode + " changed retry rejected");
        var changed = Command(InventoryBagLayout.SortOperation, owner, sorted.Records.Select(i => new ItemRecordMutation(i.Revision,
            i with { State = i.State with { Quantity = 2 } })).ToArray());
        Check((await repository.CommitAsync(changed, now, default)).Status == ItemRepositoryCommitStatus.ValidationFailed, mode + " sort cannot change quantities");
        var stale = request with { RequestId = Guid.NewGuid() };
        Check((await repository.CommitAsync(stale, now, default)).Status == ItemRepositoryCommitStatus.RevisionConflict, mode + " stale entire plan rejected");
        Check(!ItemRecordRules.TryValidateCommit(request with { Operation = "Item.Move" }, out _), "ordinary mutations stay capped at 16");
    }
    snapshot = await repository.FindByOwnerAsync(owner, false, 500, default);
    Check(snapshot.Count == 240 && snapshot.All(i => i.Revision == 2 && InventoryBagLayout.CanStore(i)), mode + " no item lost across tabs");
    var potion = snapshot.First(i => InventoryBagLayout.Tab(i.DefinitionName) == 1);
    var noMove = new ItemRecordMutation(potion.Revision, potion);
    Check((await repository.CommitAsync(Command(InventoryBagLayout.SortOperation, Owner(), [noMove]), now, default)).Status == ItemRepositoryCommitStatus.ValidationFailed,
        mode + " sorting actor must own every record");
    var rebound = potion with { Metadata = potion.Metadata with { BindState = ItemBindState.CharacterBound } };
    Check((await repository.CommitAsync(Command(InventoryBagLayout.SortOperation, owner, [new(potion.Revision, rebound)]), now, default)).Status == ItemRepositoryCommitStatus.ValidationFailed,
        mode + " sorting cannot change binding");
    Check(!ItemRecordRules.TryValidateCommit(Command(InventoryBagLayout.SortOperation, owner, Enumerable.Repeat(noMove, 81).ToArray()), out _), mode + " sort capped at 80");
    var cross = Command("Item.Move", owner, [new(potion.Revision, potion with { Location = potion.Location with { SlotIndex = 0 } })]);
    Check((await repository.CommitAsync(cross, now, default)).Status == ItemRepositoryCommitStatus.ValidationFailed, mode + " cross-tab move rejected");
    var grantOwner = Owner();
    var fullConsume = Enumerable.Range(80, 80).Select(slot => Item(grantOwner, "GameItem.Consume.Potion.Red", slot, 10) with { Revision = 1 }).ToArray();
    var attachment = Item(grantOwner, "GameItem.Consume.Potion.Red", 0, 10) with { Location = new(ItemContainerType.Mail, "reward", 0) };
    var grant = Command("DungeonReward", grantOwner, [new(0, attachment)]);
    var overflow = DungeonRewardDeliveryPlanner.Plan(grant, [10], fullConsume);
    Check(overflow.Mutations.Single().NewRecord.Location.ContainerType == ItemContainerType.Mail, "full consumables cannot borrow equipment/other space");
    var equipment = attachment with { DefinitionName = "GameItem.Equipment.Weapon.A" };
    var available = DungeonRewardDeliveryPlanner.Plan(grant with { Mutations = [new(0, equipment)] }, [10], fullConsume);
    Check(available.Mutations.Single().NewRecord.Location.SlotIndex == 0 && available.Mutations.Single().NewRecord.Location.ContainerType == ItemContainerType.Inventory, "equipment still has its own 80 slots");
    Console.WriteLine($"PASS {mode} independent 80x3, full sort CAS, receipts, immutable fields and overflow");
}

try
{
    await Run(new InMemoryItemRepository(new InMemoryTransactionGate()), "Memory");
    string? connection = Environment.GetEnvironmentVariable("PROJECTRPG_INVENTORY_TEST_DB");
    if (!string.IsNullOrEmpty(connection))
    {
        await using var source = NpgsqlDataSource.Create(connection);
        var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
        Check(await PostgresSchemaMigrations.ApplyAsync(source, catalog, default) == 11, "fresh catalog v11");
        await Run(new PostgresItemRepository(source, NullLogger<PostgresItemRepository>.Instance), "PostgreSQL");
        // Upgrade a genuinely old mixed layout; preserve identity/quantity/metadata and old receipts.
        var repo = new PostgresItemRepository(source, NullLogger<PostgresItemRepository>.Instance);
        var owner = Owner();
        var legacy = new[] { Item(owner, "GameItem.Consume.Potion.A", 0, 7), Item(owner, "GameItem.Material.Ore", 1, 9), Item(owner, "GameItem.Equipment.Weapon.A", 2) };
        var seed = Command("Test.Legacy", owner, legacy.Select(i => new ItemRecordMutation(0, i)).ToArray());
        await repo.CommitAsync(seed, now, default);
        // Model an immutable receipt written by the old release, before tab destinations existed.
        await using (var markOldReceipt = source.CreateCommand("UPDATE item_transaction_receipts SET operation='Item.Move' WHERE request_id=@id"))
        {
            markOldReceipt.Parameters.AddWithValue("id", seed.RequestId);
            await markOldReceipt.ExecuteNonQueryAsync();
        }
        seed = seed with { Operation = "Item.Move" };
        await PostgresSchemaInitializer.ApplyAsync(source, catalog[^1].Sql, default);
        var migrated = await repo.FindByOwnerAsync(owner, false, 500, default);
        Check(migrated.Count == 3 && migrated.All(InventoryBagLayout.CanStore), "legacy layout upgraded");
        Check(migrated.All(i => legacy.Single(old => old.ItemId == i.ItemId).State.Quantity == i.State.Quantity), "migration quantities/IDs preserved");
        Check((await repo.TryGetCommitResultAsync(seed.RequestId, default))!.Records.Select(i => i.Location.SlotIndex).SequenceEqual(new[] {0, 1, 2}), "historical receipts unchanged");
        Check((await new ItemTransactionService(repo, TimeProvider.System).CommitAsync(seed, default)).Status == ItemRepositoryCommitStatus.AlreadyCommitted,
            "old-layout command can still replay its original receipt after migration");
        Check((await repo.FindByOwnerAsync(owner, false, 500, default)).All(InventoryBagLayout.CanStore), "old receipt never rewrites migrated locations");
        await PostgresSchemaInitializer.ApplyAsync(source, catalog[^1].Sql, default);
        Check((await repo.FindByOwnerAsync(owner, false, 500, default)).Select(i => i.Revision).SequenceEqual(migrated.Select(i => i.Revision)), "migration idempotent");
        var oversizedOwner = Owner();
        var oversized = Enumerable.Range(0, 81).Select(slot => Item(oversizedOwner, "GameItem.Equipment.Weapon.Test", slot)).ToArray();
        foreach (var batch in oversized.Chunk(16)) await repo.CommitAsync(Command("Test.Oversized", oversizedOwner, batch.Select(i => new ItemRecordMutation(0, i)).ToArray()), now, default);
        bool rejected = false;
        try { await PostgresSchemaInitializer.ApplyAsync(source, catalog[^1].Sql, default); }
        catch (PostgresException error) when (error.SqlState == "P0001") { rejected = true; }
        Check(rejected, "oversized legacy tab rejected without dropping items");
        var unchanged = await repo.FindByOwnerAsync(oversizedOwner, false, 500, default);
        Check(unchanged.Count == 81 && unchanged.All(i => i.Revision == 1), "failed upgrade rolls back every change");
        Console.WriteLine("PASS PostgreSQL legacy upgrade and repeat initialization");
    }
    Console.WriteLine($"PASS inventory tabs: {checks} checks");
    return 0;
}
catch (Exception error) { Console.Error.WriteLine(error); return 1; }
