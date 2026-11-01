using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class EquipmentPersistenceCases
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private static readonly Type Mapper = typeof(ItemRecord).Assembly.GetType("ProjectRpg.Backend.Api.ItemApiContractMapper", true)!;
    private static readonly MethodInfo ToContract = Mapper.GetMethod("ToContract", [typeof(ItemRecord)])!;
    private static readonly MethodInfo ToDomain = Mapper.GetMethods().Single(method => method.Name == "TryToDomain"
        && method.GetParameters()[0].ParameterType == typeof(ItemCommitRequestContract));

    public static async Task RunAsync(bool postgresOnly)
    {
        List<string> groups = [];
        if (!postgresOnly)
        {
            ItemRecord legacy = Record(null);
            string oldJson = JsonSerializer.Serialize(legacy, Json);
            Require(!oldJson.Contains("equipmentProgression", StringComparison.Ordinal), "Legacy domain JSON shape changed.");
            Require(JsonSerializer.Deserialize<ItemRecord>(oldJson, Json)!.State.EquipmentProgression is null,
                "Legacy item JSON was implicitly initialized.");
            Require(Map(JsonSerializer.Serialize(Contract(legacy), Json), out ItemRecord restored)
                && restored.State.EquipmentProgression is null, "Legacy contract no longer loads.");
            JsonObject explicitNull = JsonNode.Parse(JsonSerializer.Serialize(Contract(legacy), Json))!.AsObject();
            explicitNull["state"]!["equipmentProgression"] = null;
            Require(Map(explicitNull.ToJsonString(), out restored) && restored.State.EquipmentProgression is null,
                "Explicit null must preserve legacy absence.");
            groups.Add("legacy_omitted_and_null_compatibility");

            for (int level = 0; level <= 3; level++)
            {
                ItemRecord item = Record(FirstSliceEquipmentProgression.Create() with { Level = level });
                Require(Map(JsonSerializer.Serialize(Contract(item), Json), out restored)
                    && restored.State.EquipmentProgression == item.State.EquipmentProgression
                    && restored.State.GenerationSeed == item.State.GenerationSeed
                    && restored.State.StatValues.SequenceEqual(item.State.StatValues)
                    && restored.Metadata == item.Metadata, "Contract lost growth or other persisted fields.");
                ItemRecord snapshot = ItemRecordRules.SnapshotWithRevision(item, 7);
                Require(snapshot.State.EquipmentProgression == item.State.EquipmentProgression && snapshot.Revision == 7,
                    "Revision snapshot lost growth.");
                string receiptJson = JsonSerializer.Serialize(snapshot, Json);
                Require(JsonSerializer.Deserialize<ItemRecord>(receiptJson, Json)!.State.EquipmentProgression == item.State.EquipmentProgression,
                    "Stored receipt JSON lost growth.");
            }
            groups.Add("all_levels_mapper_snapshot_and_receipt_json");

            string[] invalid = ["{}", "[]", "42", "\"bad\"",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1}",
                "{\"growthBandId\":\"FirstSlice\",\"level\":0}",
                "{\"growthBandId\":null,\"ruleVersion\":1,\"level\":0}",
                "{\"growthBandId\":\"firstslice\",\"ruleVersion\":1,\"level\":0}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":2,\"level\":0}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1,\"level\":-1}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1,\"level\":4}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1,\"level\":1.5}"];
            foreach (string growth in invalid)
            {
                JsonObject payload = JsonNode.Parse(JsonSerializer.Serialize(Contract(legacy), Json))!.AsObject();
                payload["state"]!["equipmentProgression"] = JsonNode.Parse(growth);
                Require(!Map(payload.ToJsonString(), out _), "Malformed growth was accepted: " + growth);
            }
            ItemRecord stacked = Record(FirstSliceEquipmentProgression.Create());
            stacked = stacked with { State = stacked.State with { Quantity = 2 } };
            Require(!Map(JsonSerializer.Serialize(Contract(stacked), Json), out _), "Progression equipment became stackable.");
            groups.Add("malformed_partial_fractional_and_stack_refusal");

            InMemoryItemRepository memory = new(new InMemoryTransactionGate());
            await RepositoryCasesAsync(memory);
            groups.Add("memory_atomic_missing_field_guard_and_exact_replay");

            IReadOnlyList<PostgresSchemaMigration> catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
            Require(catalog.Count == 2 && catalog[0].Version == 1 && catalog[1].Version == 2,
                "Packaged baseline/delta catalog is incomplete.");
            Require(PostgresSchemaMigrations.GetPending(catalog,
                [new(catalog[0].Version, catalog[0].Name, catalog[0].Sha256)]).Single().Version == 2,
                "A version-one database must require only the equipment delta.");
            groups.Add("packaged_version_two_after_immutable_baseline");
        }
        else
        {
            await PostgresCasesAsync(groups);
        }
        Console.WriteLine(JsonSerializer.Serialize(new { passed = true, scope = postgresOnly ? "owned PostgreSQL equipment persistence" : "mapper/domain/Memory only", groups }));
    }

    private static async Task RepositoryCasesAsync(IItemRepository repository)
    {
        ItemTransactionService service = new(repository, TimeProvider.System);
        ItemRecord equipment = Record(FirstSliceEquipmentProgression.Create() with { Level = 2 });
        ItemRecord legacy = Record(null) with { Owner = equipment.Owner,
            Location = equipment.Location with { SlotIndex = 1 } };
        ItemRepositoryCommitRequest create = new(Guid.NewGuid(), "EquipmentPersistenceFixture", "fixed-original-command",
            equipment.Owner, 0, [new(0, equipment), new(0, legacy)]);
        ItemRepositoryCommitResult created = await service.CommitAsync(create, default);
        Require(created.Status == ItemRepositoryCommitStatus.Committed, "Fixture creation failed.");
        ItemRecord stored = created.Records[0], other = created.Records[1];
        ItemRepositoryCommitRequest different = create with { Mutations = [new(0,
            equipment with { State = equipment.State with { EquipmentProgression = equipment.State.EquipmentProgression! with { Level = 3 } } }), new(0, legacy)] };
        Require((await service.CommitAsync(different, default)).Status == ItemRepositoryCommitStatus.IdempotencyConflict,
            "Changed growth bypassed exact receipt comparison.");
        different = create with { Mutations = [new(0, equipment with { State = equipment.State with { EquipmentProgression = null } }), new(0, legacy)] };
        Require((await service.CommitAsync(different, default)).Status == ItemRepositoryCommitStatus.IdempotencyConflict,
            "Omitted growth bypassed receipt comparison.");

        ItemRepositoryCommitRequest erase = new(Guid.NewGuid(), "MoveItem", "missing-field-must-not-erase",
            stored.Owner, 0, [new(stored.Revision, stored with { State = stored.State with { EquipmentProgression = null } }),
                new(other.Revision, other with { Location = other.Location with { SlotIndex = 5 } })]);
        Require((await service.CommitAsync(erase, default)).Status == ItemRepositoryCommitStatus.ValidationFailed,
            "An old client erased stored growth.");
        Require((await repository.FindAsync(stored.ItemId, default))!.Revision == stored.Revision
            && (await repository.FindAsync(other.ItemId, default))!.Location == other.Location
            && await repository.TryGetCommitResultAsync(erase.RequestId, default) is null,
            "Rejected omission changed either item or wrote a success receipt.");

        ItemRecord moved = stored with { Location = stored.Location with { SlotIndex = 3 } };
        ItemRepositoryCommitResult movedResult = await service.CommitAsync(Command(moved), default);
        Require(movedResult.Status == ItemRepositoryCommitStatus.Committed
            && movedResult.Records[0].State.EquipmentProgression == equipment.State.EquipmentProgression,
            "Ordinary move lost growth.");
        Require((await service.CommitAsync(create, default)).Status == ItemRepositoryCommitStatus.AlreadyCommitted,
            "A later move invalidated the original successful command.");
        ItemRepositoryCommitResult receipt = (await repository.TryGetCommitResultAsync(create.RequestId, default))!;
        Require(receipt.Records[0].Revision == 1 && receipt.Records[0].State.EquipmentProgression == equipment.State.EquipmentProgression,
            "The original receipt was rewritten with current state.");

        ItemRecord terminal = movedResult.Records[0] with { LifecycleState = ItemLifecycleState.Destroyed,
            Location = new(ItemContainerType.Terminal, "", -1), State = movedResult.Records[0].State with { Quantity = 0 } };
        ItemRepositoryCommitResult ended = await service.CommitAsync(Command(terminal), default);
        Require(ended.Status == ItemRepositoryCommitStatus.Committed
            && (await repository.FindAsync(terminal.ItemId, default))!.State.EquipmentProgression == equipment.State.EquipmentProgression,
            "Terminal history lost growth.");
    }

    private static async Task PostgresCasesAsync(List<string> groups)
    {
        Require(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_RUN_ID"), "N", out _),
            "PostgreSQL mode requires the owned disposable runner.");
        NpgsqlConnectionStringBuilder configuration = new(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES")
            ?? throw new InvalidOperationException("Owned PostgreSQL connection is missing."));
        Require(configuration.Host is "127.0.0.1" or "localhost" && configuration.Database == "equipment_test",
            "Refusing a shared or remote database.");
        IReadOnlyList<PostgresSchemaMigration> catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
        ItemRecord persisted = Record(FirstSliceEquipmentProgression.Create() with { Level = 2 });
        Guid legacyId = Guid.NewGuid();
        await using (NpgsqlDataSource source = NpgsqlDataSource.Create(configuration.ConnectionString))
        {
            Require(await PostgresSchemaMigrations.ApplyAsync(source, [catalog[0]], default) == 1, "Version one was not installed.");
            await using (NpgsqlCommand seed = source.CreateCommand("""
                INSERT INTO item_records (item_id, definition_type, definition_name, definition_version,
                    owner_type, owner_id, container_type, container_id, slot_index, generation_seed,
                    quantity, revision, lifecycle_state, bind_state, created_at, updated_at)
                VALUES (@id, 'RPGItemDefinition', 'Equipment.LegacyFixture', 1,
                    'Character', @owner, 'Inventory', @owner, 20, 123, 1, 1,
                    'Active', 'CharacterBound', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP);
                """))
            {
                seed.Parameters.AddWithValue("id", legacyId);
                seed.Parameters.AddWithValue("owner", Guid.NewGuid().ToString("D"));
                await seed.ExecuteNonQueryAsync();
            }
            Require(await PostgresSchemaMigrations.ApplyAsync(source, catalog, default) == 1, "Version two was not appended.");
            Require(await PostgresSchemaMigrations.ApplyAsync(source, catalog, default) == 0, "Migration was not idempotent.");
            await PostgresSchemaMigrations.ValidateAsync(source, catalog, default);
            PostgresItemRepository repository = new(source, NullLogger<PostgresItemRepository>.Instance);
            Require((await repository.FindAsync(legacyId, default))!.State.EquipmentProgression is null,
                "Legacy SQL rows were backfilled as equipment.");
            groups.Add("postgres_v1_to_v2_legacy_preservation_and_noop");
            await RepositoryCasesAsync(repository);
            groups.Add("postgres_atomic_omission_guard_exact_replay_and_terminal_history");
            ItemRepositoryCommitResult created = await new ItemTransactionService(repository, TimeProvider.System).CommitAsync(Command(persisted), default);
            Require(created.Status == ItemRepositoryCommitStatus.Committed, "Persistent fixture creation failed.");
            persisted = created.Records.Single();
        }
        await using (NpgsqlDataSource reopened = NpgsqlDataSource.Create(configuration.ConnectionString))
        {
            PostgresItemRepository repository = new(reopened, NullLogger<PostgresItemRepository>.Instance);
            ItemRecord restored = (await repository.FindAsync(persisted.ItemId, default))!;
            Require(restored.State.EquipmentProgression == persisted.State.EquipmentProgression && restored.State.GenerationSeed == persisted.State.GenerationSeed,
                "Fresh connection pool lost growth or other item state.");
            groups.Add("postgres_fresh_pool_readback");
            ItemRecord a = restored with { Location = restored.Location with { SlotIndex = 6 },
                State = restored.State with { EquipmentProgression = restored.State.EquipmentProgression! with { Level = 1 } } };
            ItemRecord b = restored with { Location = restored.Location with { SlotIndex = 7 },
                State = restored.State with { EquipmentProgression = restored.State.EquipmentProgression! with { Level = 3 } } };
            ItemTransactionService service = new(repository, TimeProvider.System);
            ItemRepositoryCommitResult[] raced = await Task.WhenAll(service.CommitAsync(Command(a), default), service.CommitAsync(Command(b), default));
            Require(raced.Count(result => result.Status == ItemRepositoryCommitStatus.Committed) == 1
                && raced.Count(result => result.Status == ItemRepositoryCommitStatus.RevisionConflict) == 1, "CAS race did not have exactly one winner.");
            ItemRecord winner = raced.Single(result => result.Status == ItemRepositoryCommitStatus.Committed).Records.Single();
            Require((await repository.FindAsync(winner.ItemId, default))!.State.EquipmentProgression == winner.State.EquipmentProgression,
                "CAS winner and persisted stage differ.");
            groups.Add("postgres_revision_race_preserves_winning_growth");
            foreach (string malformed in new[] { "null", "[]", "{}", "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1,\"level\":4}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1,\"level\":1.5}", "{\"growthBandId\":\"Other\",\"ruleVersion\":1,\"level\":0}" })
            {
                await using NpgsqlCommand bad = reopened.CreateCommand("UPDATE item_records SET equipment_progression = @growth WHERE item_id = @id;");
                bad.Parameters.AddWithValue("growth", NpgsqlDbType.Jsonb, malformed);
                bad.Parameters.AddWithValue("id", winner.ItemId);
                bool rejected = false;
                try { await bad.ExecuteNonQueryAsync(); }
                catch (PostgresException failure) when (failure.SqlState == "23514") { rejected = true; }
                Require(rejected, "SQL allowed malformed progression.");
            }
            Require((await repository.FindAsync(winner.ItemId, default))!.State.EquipmentProgression == winner.State.EquipmentProgression,
                "Rejected direct SQL changed growth.");
            groups.Add("postgres_json_constraint_and_failed_write_preservation");
            string init = await PostgresSchemaMigrations.LoadInitializationSqlAsync(AppContext.BaseDirectory, default);
            await PostgresSchemaInitializer.ApplyAsync(reopened, init, default);
            await PostgresSchemaInitializer.ApplyAsync(reopened, init, default);
            Require((await repository.FindAsync(winner.ItemId, default))!.State.EquipmentProgression == winner.State.EquipmentProgression,
                "Development initialization replay changed growth.");
            groups.Add("postgres_development_initialization_replay");
        }
    }

    private static ItemRecord Record(FirstSliceEquipmentProgressionState? growth)
    {
        string owner = Guid.NewGuid().ToString("D");
        return new("RPGItemDefinition", "Equipment.PersistenceFixture", 1, new(ItemOwnerType.Character, owner),
            new(ItemContainerType.Inventory, owner, 0), new(Guid.NewGuid(), 123, 1, ["Instance.Preserved"],
                [new("Shared.Stat.Power", 7.5)], growth), 0, ItemLifecycleState.Active,
            new(ItemBindState.CharacterBound, new(0, 0), null, "PersistenceFixture", false));
    }

    private static ItemRepositoryCommitRequest Command(ItemRecord record) => new(Guid.NewGuid(), "PersistenceFixture",
        "fixture-" + record.ItemId.ToString("D"), record.Owner, 0, [new(record.Revision, record)]);

    private static ItemRecordContract Contract(ItemRecord record) => (ItemRecordContract)ToContract.Invoke(null, [record])!;

    private static bool Map(string itemJson, out ItemRecord record)
    {
        record = null!;
        try
        {
            ItemRecordContract item = JsonSerializer.Deserialize<ItemRecordContract>(itemJson, Json)!;
            ItemCommitRequestContract command = new(Guid.NewGuid(), "PersistenceFixture", "mapping-fixture", item.Owner, 0, [new(item.Revision, item)]);
            object?[] values = [command, null, null];
            if (!(bool)ToDomain.Invoke(null, values)!) return false;
            ItemRepositoryCommitRequest domain = (ItemRepositoryCommitRequest)values[1]!;
            if (!ItemRecordRules.TryValidateCommit(domain, out _)) return false;
            record = domain.Mutations.Single().NewRecord;
            return true;
        }
        catch (JsonException) { return false; }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
