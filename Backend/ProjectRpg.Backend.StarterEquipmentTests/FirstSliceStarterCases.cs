using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class FirstSliceStarterCases
{
    private static readonly IReadOnlyDictionary<int, string> Expected = new Dictionary<int, string>
    {
        [9] = "GameItem.Equipment.Progression.Starter.GreatSword",
        [1] = "GameItem.Equipment.Progression.Starter.Chest",
        [4] = "GameItem.Equipment.Progression.Starter.Gloves"
    };

    internal static void Verify(IReadOnlyList<ItemRecord> records, Guid character)
    {
        Require(records.Count == 3 && records.Select(x => x.ItemId).Distinct().Count() == 3
            && records.Select(x => x.Location.SlotIndex).Distinct().Count() == 3, "Three independent starter items required.");
        string owner = character.ToString("D");
        foreach (ItemRecord item in records)
        {
            Require(Expected.TryGetValue(item.Location.SlotIndex, out string? name) && item.DefinitionName == name
                && item.DefinitionType == "RPGItemDefinition" && item.DefinitionVersion == 1
                && item.Owner == new ItemOwnerRef(ItemOwnerType.Character, owner)
                && item.Location.ContainerType == ItemContainerType.Equipment && item.Location.ContainerId == owner
                && item.ItemId != Guid.Empty && item.State.Quantity == 1 && item.Revision == 1
                && item.LifecycleState == ItemLifecycleState.Active && item.Metadata.BindState == ItemBindState.CharacterBound
                && item.Metadata.CreationSource == "CharacterCreation" && !item.Metadata.IsLocked && item.Metadata.ExpiresAtUtc is null
                && item.State.EquipmentProgression == new FirstSliceEquipmentProgressionState("FirstSlice", 1, 0)
                && item.State.InstanceTags.Count == 0 && item.State.StatValues.Count == 0,
                "Creation must grant only version-one equipped starters with explicit +0 and empty rolled stats.");
        }
    }

    public static async Task RunAsync(bool postgres)
    {
        List<string> groups = [];
        IReadOnlyList<ItemRecord> recipe = FirstSliceStarterEquipment.Create(Guid.NewGuid());
        Verify(recipe, Guid.Parse(recipe[0].Owner.OwnerId));
        using (JsonDocument manifest = JsonDocument.Parse(File.ReadAllText(
            Environment.GetEnvironmentVariable("RPG_STARTER_MANIFEST") ?? throw new InvalidOperationException("Private manifest missing."))))
        {
            foreach (ItemRecord item in recipe)
            {
                JsonElement authored = manifest.RootElement.GetProperty("assets").EnumerateArray().Single(asset =>
                    asset.GetProperty("properties").GetProperty("expected_primary_asset_id").GetString()
                        == item.DefinitionType + ":" + item.DefinitionName);
                Require(authored.GetProperty("properties").GetProperty("slot_index").GetInt32() == item.Location.SlotIndex
                    && authored.GetProperty("properties").GetProperty("definition_version").GetInt32() == item.DefinitionVersion,
                    "Recipe disagrees with actual authored starter definitions.");
            }
        }
        bool emptyRejected = false;
        try { FirstSliceStarterEquipment.Create(Guid.Empty); }
        catch (ArgumentException) { emptyRejected = true; }
        Require(emptyRejected, "Empty character ID accepted.");
        groups.Add("exact_three_authored_starters_explicit_zero_unique_ids_and_slots");
        if (!postgres)
        {
            InMemoryTransactionGate gate = new();
            InMemoryItemRepository items = new(gate);
            InMemoryGameRepository game = new(gate, items);
            await CreationAsync(game, items, groups);
            MethodInfo seed = typeof(InMemoryItemRepository).GetMethod("SeedStarters", BindingFlags.Instance | BindingFlags.NonPublic)!;
            foreach (bool storedConflict in new[] { false, true })
            {
                InMemoryItemRepository isolated = new(new InMemoryTransactionGate());
                ItemRecord[] batch = FirstSliceStarterEquipment.Create(Guid.NewGuid()).ToArray();
                if (storedConflict)
                    Require((await isolated.CommitAsync(new(Guid.NewGuid(), "StarterFixture", "fixture", batch[1].Owner,
                        0, [new(0, batch[1] with { Revision = 0 })]), DateTimeOffset.UtcNow, default)).Status
                        == ItemRepositoryCommitStatus.Committed, "Conflict fixture failed.");
                else batch[1] = batch[1] with { Location = batch[0].Location };
                bool rejected = false;
                try { seed.Invoke(isolated, [batch]); }
                catch (TargetInvocationException error) when (error.InnerException is InvalidOperationException) { rejected = true; }
                Require(rejected && await isolated.FindAsync(batch[0].ItemId, default) is null
                    && (await isolated.FindByOwnerAsync(batch[0].Owner, false, 100, default)).Count == (storedConflict ? 1 : 0),
                    "Later starter collision partially published the first item.");
            }
            groups.Add("memory_whole_batch_preflight_before_any_publish");
        }
        else
        {
            Require(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_RUN_ID"), "N", out _), "Owned PG run missing.");
            NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES"));
            Require(config.Host == "127.0.0.1" && config.Database == "equipment_test", "Refusing shared/remote DB.");
            await using NpgsqlDataSource source = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository items = new(source, NullLogger<PostgresItemRepository>.Instance);
            PostgresGameRepository game = new(source, NullLogger<PostgresGameRepository>.Instance, equipmentItems: items);
            await game.EnsureCreatedAsync(default);
            await CreationAsync(game, items, groups);
            string steam = "stfail-" + Guid.NewGuid().ToString("N")[..8];
            DateTimeOffset now = DateTimeOffset.UtcNow;
            await game.UpsertAccountAsync(new(steam, steam, false, false), now, default);
            await using (NpgsqlCommand inject = source.CreateCommand("""
                CREATE FUNCTION reject_starter_chest() RETURNS TRIGGER LANGUAGE plpgsql AS $$
                BEGIN
                  IF NEW.creation_source='CharacterCreation' AND NEW.slot_index=1 THEN
                    RAISE EXCEPTION 'owned fixture second-starter failure';
                  END IF;
                  RETURN NEW;
                END $$;
                CREATE TRIGGER reject_starter_chest BEFORE INSERT ON item_records
                FOR EACH ROW EXECUTE FUNCTION reject_starter_chest();
                """)) await inject.ExecuteNonQueryAsync();
            try
            {
                bool rejected = false;
                try { await game.CreateCharacterAsync(steam, "AtomicStarterFail", now, default); }
                catch (PostgresException error) when (error.SqlState == PostgresErrorCodes.RaiseException) { rejected = true; }
                Require(rejected && (await game.GetCharactersAsync(steam, default)).Count == 0, "Character survived a later starter failure.");
                await using NpgsqlCommand audit = source.CreateCommand("""
                    SELECT (SELECT COUNT(*) FROM item_records WHERE owner_id NOT IN
                              (SELECT character_id::TEXT FROM characters) AND creation_source='CharacterCreation')
                         + (SELECT COUNT(*) FROM character_progression p LEFT JOIN characters c USING(character_id)
                              WHERE c.character_id IS NULL);
                    """);
                Require(Convert.ToInt64(await audit.ExecuteScalarAsync()) == 0, "Failed creation left an orphan item/progression record.");
            }
            finally
            {
                await using NpgsqlCommand remove = source.CreateCommand("DROP TRIGGER reject_starter_chest ON item_records; DROP FUNCTION reject_starter_chest();");
                await remove.ExecuteNonQueryAsync();
            }
            GameCharacter retried = await game.CreateCharacterAsync(steam, "AtomicStarterFail", now, default);
            await using NpgsqlDataSource fresh = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository reopened = new(fresh, NullLogger<PostgresItemRepository>.Instance);
            Verify(await reopened.FindByOwnerAsync(new(ItemOwnerType.Character, retried.CharacterId.ToString("D")), false, 100, default), retried.CharacterId);
            groups.Add("postgres_second_item_failure_rolls_back_character_items_and_progression");
            groups.Add("postgres_retry_and_fresh_pool_read_three_persisted_zero_states");
        }
        Console.WriteLine(JsonSerializer.Serialize(new { passed = true, candidateOnly = true, scope = postgres ? "owned PG candidate" : "Memory candidate", groups }));
    }

    private static async Task CreationAsync(IGameRepository game, IItemRepository items, List<string> groups)
    {
        string suffix = Guid.NewGuid().ToString("N")[..8], steam = "starter-" + suffix;
        DateTimeOffset now = DateTimeOffset.UtcNow;
        await game.UpsertAccountAsync(new(steam, steam, false, false), now, default);
        GameCharacter character = await game.CreateCharacterAsync(steam, "Starter" + suffix, now, default);
        ItemOwnerRef owner = new(ItemOwnerType.Character, character.CharacterId.ToString("D"));
        Verify(await items.FindByOwnerAsync(owner, false, 100, default), character.CharacterId);
        CharacterProgressionState progression = (await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, now, default))!;
        Require(progression.Experience == 0 && FirstSliceProgression.Level(progression) == 1
            && FirstSliceProgression.TotalSP(progression) == 0, "Equipment creation changed character growth.");
        bool duplicate = false;
        try { await game.CreateCharacterAsync(steam, ("Starter" + suffix).ToLowerInvariant(), now, default); }
        catch (DuplicateCharacterNameException) { duplicate = true; }
        Require(duplicate && (await game.GetCharactersAsync(steam, default)).Count == 1, "Duplicate name created extra character.");
        Verify(await items.FindByOwnerAsync(owner, false, 100, default), character.CharacterId);
        groups.Add("new_character_three_equipped_zero_states_and_duplicate_name_no_extra_grants");
        ItemRecord legacy = StarterEquipment.Create(character.CharacterId, Guid.NewGuid()) with
            { Revision = 0, Location = new(ItemContainerType.Inventory, owner.OwnerId, 50) };
        ItemRepositoryCommitResult stored = await items.CommitAsync(new(Guid.NewGuid(), "LegacyFixture", "legacy", owner, 0, [new(0, legacy)]), now, default);
        Require(stored.Status == ItemRepositoryCommitStatus.Committed, "Legacy fixture creation failed.");
        string before = JsonSerializer.Serialize(await items.FindAsync(legacy.ItemId, default));
        GameCharacter second = await game.CreateCharacterAsync(steam, "Sibling" + suffix, now, default);
        Verify(await items.FindByOwnerAsync(new(ItemOwnerType.Character, second.CharacterId.ToString("D")), false, 100, default), second.CharacterId);
        Require(JsonSerializer.Serialize(await items.FindAsync(legacy.ItemId, default)) == before
            && (await items.FindAsync(legacy.ItemId, default))!.State.EquipmentProgression is null,
            "New creation migrated an existing legacy item.");
        groups.Add("new_sibling_does_not_migrate_existing_legacy_identity_or_missing_growth");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
