using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class EquipmentLobbyCases
{
    internal sealed record Fixture(string Steam, GameCharacter Character, ItemRecord Equipment, ItemRecord Target, ItemRecord Ore);

    public static async Task RunAsync(bool postgres)
    {
        List<string> groups = [];
        if (!postgres)
        {
            InMemoryTransactionGate gate = new();
            InMemoryItemRepository items = new(gate);
            InMemoryGameRepository game = new(gate, items);
            await StoreCasesAsync(game, items, game, groups);
            await EquipmentLobbyHttpCases.RunAsync(groups);
        }
        else
        {
            Require(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_RUN_ID"), "N", out _), "Owned PG run missing.");
            NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES"));
            Require(config.Host == "127.0.0.1" && config.Database == "equipment_test", "Refusing a shared or remote DB.");
            await using NpgsqlDataSource data = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository items = new(data, NullLogger<PostgresItemRepository>.Instance);
            PostgresGameRepository game = new(data, NullLogger<PostgresGameRepository>.Instance, equipmentItems: items);
            await game.EnsureCreatedAsync(default);
            await StoreCasesAsync(game, items, game, groups);
            await AdmissionLockAsync(data, game, items, groups);
            await LeaseCleanupLockAsync(data, game, items, groups);
            // Fresh connections/repositories must recover the persisted narrow receipt.
            Fixture f = await NewFixtureAsync(game, items);
            FirstSliceEquipmentUpgradeCommand command = Upgrade(f);
            await game.UpgradeEquipmentAsync(f.Steam, command, DateTimeOffset.UtcNow, default);
            await using NpgsqlDataSource reopened = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository reitems = new(reopened, NullLogger<PostgresItemRepository>.Instance);
            PostgresGameRepository regame = new(reopened, NullLogger<PostgresGameRepository>.Instance, equipmentItems: reitems);
            Require((await regame.UpgradeEquipmentAsync(f.Steam, command, DateTimeOffset.UtcNow, default)).Status
                == ItemRepositoryCommitStatus.AlreadyCommitted, "Restart lost the receipt.");
            groups.Add("postgres_fresh_pool_exact_receipt_recovery");
        }
        Console.WriteLine(JsonSerializer.Serialize(new { passed = true, scope = postgres ? "owned PostgreSQL lobby transactions" : "Memory and real bearer HTTP", groups }));
    }

    private static async Task StoreCasesAsync(IGameRepository game, IItemRepository items,
        IEquipmentProgressionRepository equipment, List<string> groups)
    {
        Fixture f = await NewFixtureAsync(game, items);
        DateTimeOffset now = DateTimeOffset.UtcNow;
        FirstSliceEquipmentUpgradeCommand original = Upgrade(f);
        await Reject(() => equipment.UpgradeEquipmentAsync("76561000000000000", original, now, default), "character_not_owned");
        ItemRepositoryCommitResult committed = await equipment.UpgradeEquipmentAsync(f.Steam, original, now, default);
        Require(committed.Status == ItemRepositoryCommitStatus.Committed
            && committed.Records[0].State.EquipmentProgression!.Level == 1
            && (await items.FindAsync(f.Ore.ItemId, default))!.State.Quantity == 18, "Owned upgrade was not atomic.");
        await Reject(() => equipment.UpgradeEquipmentAsync(f.Steam, original with
            { Equipment = original.Equipment with { Level = 1 } }, now, default), "idempotency_conflict");
        // Moving after success must not make a lost response unrecoverable.
        ItemRecord current = (await items.FindAsync(f.Equipment.ItemId, default))!;
        ItemRepositoryCommitRequest move = new(Guid.NewGuid(), "MoveItem", "move-after-upgrade", current.Owner, 0,
            [new(current.Revision, current with { Location = current.Location with { SlotIndex = 24 } })]);
        Require((await items.CommitAsync(move, now, default)).Status == ItemRepositoryCommitStatus.Committed, "Move fixture failed.");
        await game.CreateDungeonSessionAsync(f.Steam, f.Character.CharacterId, "EquipmentFixture", "Normal", now, now.AddHours(1), default);
        ItemRepositoryCommitResult replay = await equipment.UpgradeEquipmentAsync(f.Steam, original, now, default);
        Require(replay.Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && replay.Records[0].Revision == 2 && replay.Records[0].Location.SlotIndex == 20,
            "Receipt was rebuilt from current state or blocked by lease.");
        ((string[])replay.Records[0].State.InstanceTags)[0] = "ForgedReturnedArray";
        Require((await equipment.UpgradeEquipmentAsync(f.Steam, original, now, default)).Records[0].State.InstanceTags[0] == "Item.Test.Preserved",
            "Returned arrays mutated the saved receipt.");
        ItemRecord moved = (await items.FindAsync(f.Equipment.ItemId, default))!;
        ItemRecord ore = (await items.FindAsync(f.Ore.ItemId, default))!;
        FirstSliceEquipmentUpgradeCommand next = Upgrade(f, moved, ore);
        await Reject(() => equipment.UpgradeEquipmentAsync(f.Steam, next, now, default), "active_dungeon_equipment_locked");
        Require((await items.FindAsync(ore.ItemId, default))!.State.Quantity == 18
            && await items.TryGetCommitResultAsync(next.RequestId, default) is null, "Lease rejection charged cost or stored success.");
        ItemRepositoryCommitResult second = await equipment.UpgradeEquipmentAsync(f.Steam, next, now.AddHours(2), default);
        Require(second.Status == ItemRepositoryCommitStatus.Committed && second.Records[0].State.EquipmentProgression!.Level == 2
            && (await items.FindAsync(ore.ItemId, default))!.State.Quantity == 14, "Expired lease did not release the command.");
        Require((await equipment.UpgradeEquipmentAsync(f.Steam, original, now.AddHours(2), default)).Records[0].Revision == 2
            && (await items.FindAsync(moved.ItemId, default))!.Revision == 4, "Old replay overwrote today's item.");
        groups.Add("ownership_success_replay_before_lease_current_state_and_copy_isolation");
        groups.Add("live_lease_blocks_new_command_and_expired_lease_allows_exact_retry");

        ItemRecord source = (await items.FindAsync(f.Equipment.ItemId, default))!;
        FirstSliceEquipmentTransferCommand transfer = new(Guid.NewGuid(), f.Character.CharacterId, 1,
            Expect(source), Expect(f.Target));
        ItemRepositoryCommitResult transferred = await equipment.TransferEquipmentAsync(f.Steam, transfer, now.AddHours(2), default);
        Require(transferred.Status == ItemRepositoryCommitStatus.Committed
            && transferred.Records[0].State.EquipmentProgression!.Level == 0
            && transferred.Records[1].State.EquipmentProgression!.Level == 2
            && transferred.Records[0].Location.SlotIndex == 24
            && transferred.Records[1].Location == f.Target.Location, "Two-item transfer was not preserved/atomic.");
        Require((await equipment.TransferEquipmentAsync(f.Steam, transfer, now.AddHours(2), default)).Status
            == ItemRepositoryCommitStatus.AlreadyCommitted, "Transfer replay applied again.");
        Fixture other = await NewFixtureAsync(game, items);
        await Reject(() => equipment.UpgradeEquipmentAsync(other.Steam, Upgrade(other) with { RequestId = original.RequestId }, now, default),
            "idempotency_conflict");
        groups.Add("two_item_transfer_and_cross_owner_request_id_collision");

        Fixture race = await NewFixtureAsync(game, items);
        FirstSliceEquipmentUpgradeCommand a = Upgrade(race);
        FirstSliceEquipmentUpgradeCommand b = Upgrade(race, race.Target, race.Ore);
        async Task<bool> Attempt(FirstSliceEquipmentUpgradeCommand command)
        {
            try { return (await equipment.UpgradeEquipmentAsync(race.Steam, command, now, default)).Status == ItemRepositoryCommitStatus.Committed; }
            catch (EquipmentProgressionRuleException error) when (error.Code == "item_revision_conflict") { return false; }
        }
        bool[] winners = await Task.WhenAll(Task.Run(() => Attempt(a)), Task.Run(() => Attempt(b)));
        Require(winners.Count(x => x) == 1 && (await items.FindAsync(race.Ore.ItemId, default))!.State.Quantity == 18
            && (await items.FindAsync(race.Equipment.ItemId, default))!.State.EquipmentProgression!.Level
               + (await items.FindAsync(race.Target.ItemId, default))!.State.EquipmentProgression!.Level == 1,
            "Racing commands spent twice or partially enhanced the loser.");
        Require(await items.TryGetCommitResultAsync(winners[0] ? b.RequestId : a.RequestId, default) is null,
            "Rejected command stored a success receipt.");
        groups.Add("concurrent_shared_ore_single_winner_no_partial_write_or_receipt");
    }

    private static async Task AdmissionLockAsync(NpgsqlDataSource data, PostgresGameRepository game,
        PostgresItemRepository items, List<string> groups)
    {
        Fixture f = await NewFixtureAsync(game, items);
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(20));
        CancellationToken ct = deadline.Token;
        await using NpgsqlConnection blocker = await data.OpenConnectionAsync(ct);
        await using NpgsqlTransaction barrier = await blocker.BeginTransactionAsync(ct);
        long key = BinaryPrimitives.ReadInt64LittleEndian(SHA256.HashData(Encoding.UTF8.GetBytes(
            "item_owner|Character|" + f.Character.CharacterId.ToString("D"))));
        await using (NpgsqlCommand hold = new("SELECT pg_advisory_xact_lock(@key);", blocker, barrier))
        { hold.Parameters.AddWithValue("key", key); await hold.ExecuteNonQueryAsync(ct); }
        Task<ItemRepositoryCommitResult> upgrade = game.UpgradeEquipmentAsync(f.Steam, Upgrade(f), DateTimeOffset.UtcNow, ct);
        bool locked = false;
        // Observe the real command's character FOR UPDATE lock while it waits for
        // our item-owner barrier; no product timing hooks or injected lease result.
        for (int attempt = 0; attempt < 100 && !locked; attempt++)
        {
            await using NpgsqlConnection probe = await data.OpenConnectionAsync(ct);
            await using NpgsqlTransaction probeTransaction = await probe.BeginTransactionAsync(ct);
            await using NpgsqlCommand query = new("SELECT character_id FROM characters WHERE character_id=@id FOR KEY SHARE NOWAIT;", probe, probeTransaction);
            query.Parameters.AddWithValue("id", f.Character.CharacterId);
            try { await query.ExecuteScalarAsync(ct); }
            catch (PostgresException error) when (error.SqlState == PostgresErrorCodes.LockNotAvailable) { locked = true; }
            await probeTransaction.RollbackAsync(ct);
            if (!locked) await Task.Delay(20, ct);
        }
        Require(locked && !upgrade.IsCompleted, "Equipment command did not hold the admission character lock.");
        DateTimeOffset now = DateTimeOffset.UtcNow;
        Task<DungeonSession> admission = game.CreateDungeonSessionAsync(f.Steam, f.Character.CharacterId,
            "EquipmentFixture", "Normal", now, now.AddMinutes(5), ct);
        await barrier.CommitAsync(ct);
        Require((await upgrade).Status == ItemRepositoryCommitStatus.Committed, "Blocked upgrade did not finish.");
        await admission;
        ItemRecord current = (await items.FindAsync(f.Equipment.ItemId, ct))!;
        ItemRecord ore = (await items.FindAsync(f.Ore.ItemId, ct))!;
        await Reject(() => game.UpgradeEquipmentAsync(f.Steam, Upgrade(f, current, ore), now, ct), "active_dungeon_equipment_locked");
        groups.Add("postgres_character_fk_lock_serializes_real_admission_after_item_commit");
    }

    private static async Task LeaseCleanupLockAsync(NpgsqlDataSource data, PostgresGameRepository game,
        PostgresItemRepository items, List<string> groups)
    {
        Fixture f = await NewFixtureAsync(game, items);
        DateTimeOffset now = DateTimeOffset.UtcNow;
        DungeonSession session = await game.CreateDungeonSessionAsync(f.Steam, f.Character.CharacterId,
            "EquipmentFixture", "Normal", now, now.AddMinutes(5), default);
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(10));
        CancellationToken ct = deadline.Token;
        // Model cleanup's delete-before-FK-insert order on our own disposable rows.
        await using NpgsqlConnection blocker = await data.OpenConnectionAsync(ct);
        await using NpgsqlTransaction cleanup = await blocker.BeginTransactionAsync(ct);
        await using (NpgsqlCommand remove = new("DELETE FROM character_session_leases WHERE character_id=@id;", blocker, cleanup))
        { remove.Parameters.AddWithValue("id", f.Character.CharacterId); await remove.ExecuteNonQueryAsync(ct); }
        FirstSliceEquipmentUpgradeCommand command = Upgrade(f);
        await Reject(() => game.UpgradeEquipmentAsync(f.Steam, command, now, ct), "equipment_lease_busy");
        // After the busy response the command must have released its character FK
        // lock, so cleanup can publish the new lease without a deadlock.
        await using (NpgsqlCommand insert = new("""
            INSERT INTO character_session_leases (character_id,dungeon_session_id,expires_at,updated_at)
            VALUES (@id,@session,@expiry,@now);
            """, blocker, cleanup))
        {
            insert.Parameters.AddWithValue("id", f.Character.CharacterId);
            insert.Parameters.AddWithValue("session", session.DungeonSessionId);
            insert.Parameters.AddWithValue("expiry", now.AddMinutes(5));
            insert.Parameters.AddWithValue("now", now);
            await insert.ExecuteNonQueryAsync(ct);
        }
        await cleanup.CommitAsync(ct);
        await Reject(() => game.UpgradeEquipmentAsync(f.Steam, command, now, ct), "active_dungeon_equipment_locked");
        Require((await items.FindAsync(f.Ore.ItemId, ct))!.State.Quantity == 20
            && (await items.FindAsync(f.Equipment.ItemId, ct))!.Revision == 1
            && await items.TryGetCommitResultAsync(command.RequestId, ct) is null,
            "Busy/lease rejection left a partial item or receipt.");
        groups.Add("postgres_lease_cleanup_lock_busy_rolls_back_without_character_fk_deadlock");
    }

    internal static async Task<Fixture> NewFixtureAsync(IGameRepository game, IItemRepository items)
    {
        string suffix = Guid.NewGuid().ToString("N")[..10];
        string steam = "equipment-" + suffix;
        DateTimeOffset now = DateTimeOffset.UtcNow;
        await game.UpsertAccountAsync(new(steam, steam, false, false), now, default);
        GameCharacter character = await game.CreateCharacterAsync(steam, "Eq" + suffix, now, default);
        ItemOwnerRef owner = new(ItemOwnerType.Character, character.CharacterId.ToString("D"));
        ItemRecord Make(string name, int slot, int count, bool equipment) => new("RPGItemDefinition", name, 1, owner,
            new(ItemContainerType.Inventory, owner.OwnerId, slot),
            new(Guid.NewGuid(), 7, count, ["Item.Test.Preserved"], [], equipment ? FirstSliceEquipmentProgression.Create() : null),
            0, ItemLifecycleState.Active, new(ItemBindState.CharacterBound, new(0, 0), null, "EquipmentLobbyFixture", false));
        ItemRecord weapon = Make("GameItem.Equipment.Progression.Starter.GreatSword", 20, 1, true);
        ItemRecord target = Make("GameItem.Equipment.Progression.Target.EnduranceGreatSword", 21, 1, true);
        ItemRecord ore = Make("GameItem.Material.Progression.Ore", 22, 20, false);
        ItemRepositoryCommitResult seeded = await items.CommitAsync(new(Guid.NewGuid(), "EquipmentLobbyFixture", "server-fixture", owner,
            0, [new(0, weapon), new(0, target), new(0, ore)]), now, default);
        Require(seeded.Status == ItemRepositoryCommitStatus.Committed, "Fixture seed failed.");
        return new(steam, character, seeded.Records[0], seeded.Records[1], seeded.Records[2]);
    }

    internal static EquipmentItemExpectation Expect(ItemRecord item) => new(item.ItemId, item.Revision, item.State.EquipmentProgression!.Level);
    internal static FirstSliceEquipmentUpgradeCommand Upgrade(Fixture f, ItemRecord? weapon = null, ItemRecord? ore = null) =>
        new(Guid.NewGuid(), f.Character.CharacterId, 1, Expect(weapon ?? f.Equipment), [new((ore ?? f.Ore).ItemId, (ore ?? f.Ore).Revision)]);
    internal static async Task Reject(Func<Task<ItemRepositoryCommitResult>> action, string code)
    {
        try { await action(); }
        catch (EquipmentProgressionRuleException error) { Require(error.Code == code, "Unexpected refusal: " + error.Code); return; }
        throw new InvalidOperationException("Unsafe equipment command accepted: " + code);
    }
    internal static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
