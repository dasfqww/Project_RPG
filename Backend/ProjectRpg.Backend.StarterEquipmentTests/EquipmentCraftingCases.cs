using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static EquipmentLobbyCases;

internal static class EquipmentCraftingCases
{
    internal sealed record CraftFixture(string Steam, GameCharacter Character, ItemOwnerRef Owner, ItemRecord Token);
    internal const string Target = "GameItem.Equipment.Progression.Target.Chest";
    internal static readonly DateTimeOffset Now = DateTimeOffset.UtcNow;

    public static async Task RunAsync(bool postgres)
    {
        List<string> groups = [];
        if (!postgres)
        {
            InMemoryTransactionGate gate = new();
            InMemoryItemRepository items = new(gate);
            InMemoryGameRepository game = new(gate, items);
            await StoreCases(game, items, game, groups);
            DomainRejections(groups);
            await EquipmentCraftingHttpCases.RunAsync(groups);
        }
        else
        {
            Require(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_RUN_ID"), "N", out _), "Missing owned run.");
            NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES"));
            Require(config.Host == "127.0.0.1" && config.Database == "equipment_test", "Refusing a shared DB.");
            await using NpgsqlDataSource data = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository items = new(data, NullLogger<PostgresItemRepository>.Instance);
            PostgresGameRepository game = new(data, NullLogger<PostgresGameRepository>.Instance, equipmentItems: items);
            await game.EnsureCreatedAsync(default);
            await StoreCases(game, items, game, groups);
            await Rollback(data, game, items, groups);
            CraftFixture f = await NewFixture(game, items);
            var command = Craft(f);
            var first = await game.CraftEquipmentAsync(f.Steam, command, Now, default);
            await using NpgsqlDataSource reopened = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository reitems = new(reopened, NullLogger<PostgresItemRepository>.Instance);
            PostgresGameRepository regame = new(reopened, NullLogger<PostgresGameRepository>.Instance, equipmentItems: reitems);
            var replay = await regame.CraftEquipmentAsync(f.Steam, command, Now, default);
            Require(replay.Status == ItemRepositoryCommitStatus.AlreadyCommitted && replay.Records.Select(x => x.ItemId).SequenceEqual(first.Records.Select(x => x.ItemId)), "Fresh pool generated another item.");
            groups.Add("postgres_fresh_pool_recovers_original_output_ids");
        }
        Console.WriteLine(JsonSerializer.Serialize(new { passed = true, scope = postgres ? "owned PostgreSQL crafting" : "Memory, planner refusal and real bearer HTTP", groups }));
    }

    private static async Task StoreCases(IGameRepository game, IItemRepository items, IEquipmentProgressionRepository equipment, List<string> groups)
    {
        foreach (string target in FirstSliceEquipmentDefinitions.All.Skip(3).Select(x => x.DefinitionName))
        {
            CraftFixture f = await NewFixture(game, items);
            var craft = Craft(f) with { TargetDefinitionName = target };
            var result = await equipment.CraftEquipmentAsync(f.Steam, craft, Now, default);
            ItemRecord gear = result.Records.Single(x => x.State.EquipmentProgression is not null);
            Require(result.Status == ItemRepositoryCommitStatus.Committed && result.AffectedQuantity == 8
                && gear.DefinitionName == target && gear.DefinitionVersion == 1 && gear.Revision == 1
                && gear.State.EquipmentProgression == FirstSliceEquipmentProgression.Create()
                && gear.State.Quantity == 1 && gear.Metadata.BindState == ItemBindState.CharacterBound
                && gear.Location == new ItemLocation(ItemContainerType.Inventory, f.Owner.OwnerId, 0)
                && (await items.FindAsync(f.Token.ItemId, default))!.LifecycleState == ItemLifecycleState.Consumed, "Craft did not consume exactly eight and deliver +0.");
            var dismantle = Dismantle(f, gear);
            var removed = await equipment.DismantleEquipmentAsync(f.Steam, dismantle, Now, default);
            ItemRecord token = removed.Records.Single(x => x.LifecycleState == ItemLifecycleState.Active);
            Require(removed.Status == ItemRepositoryCommitStatus.Committed && removed.AffectedQuantity == 1
                && token.DefinitionName == FirstSliceEquipmentDefinitions.TokenDefinitionName && token.State.Quantity == 1
                && token.State.EquipmentProgression is null && token.Location.SlotIndex == 0
                && (await items.FindAsync(gear.ItemId, default))!.LifecycleState == ItemLifecycleState.Consumed, "Dismantle did not consume gear and return one token.");
            Require((await equipment.DismantleEquipmentAsync(f.Steam, dismantle, Now, default)).Status == ItemRepositoryCommitStatus.AlreadyCommitted, "Dismantle replay charged again.");
            await game.CreateDungeonSessionAsync(f.Steam, f.Character.CharacterId, "CraftFixture", "Normal", Now, Now.AddHours(1), default);
            var replay = await equipment.CraftEquipmentAsync(f.Steam, craft, Now, default);
            Require(replay.Status == ItemRepositoryCommitStatus.AlreadyCommitted && replay.Records.Single(x => x.ItemId == gear.ItemId).LifecycleState == ItemLifecycleState.Active, "Craft receipt rebuilt from consumed gear/blocked by lease.");
            await Reject(() => equipment.CraftEquipmentAsync(f.Steam, craft with { TargetDefinitionName = "unknown" }, Now, default), "idempotency_conflict");
            await Reject(() => equipment.CraftEquipmentAsync(f.Steam, Craft(f, token), Now, default), "active_dungeon_equipment_locked");
        }
        groups.Add("four_exact_targets_atomic_craft_dismantle_and_receipt_before_current_state_lease");

        CraftFixture multi = await NewFixture(game, items, 4);
        ItemRecord second = (await Seed(items, multi.Owner, [Make(multi.Owner, FirstSliceEquipmentDefinitions.TokenDefinitionName, 1, 4)]))[0];
        var multiCommand = Craft(multi) with { TokenStacks = [new(second.ItemId, second.Revision), new(multi.Token.ItemId, multi.Token.Revision)] };
        Require((await equipment.CraftEquipmentAsync(multi.Steam, multiCommand, Now, default)).Status == ItemRepositoryCommitStatus.Committed, "Split token cost failed.");
        Require((await equipment.CraftEquipmentAsync(multi.Steam, multiCommand with { TokenStacks = multiCommand.TokenStacks.Reverse().ToArray() }, Now, default)).Status == ItemRepositoryCommitStatus.AlreadyCommitted, "Token ordering changed idempotency.");
        groups.Add("split_stack_cost_and_normalized_request_order");

        CraftFixture race = await NewFixture(game, items);
        var a = Craft(race); var b = Craft(race);
        async Task<bool> Attempt(FirstSliceEquipmentCraftCommand command)
        {
            try { return (await equipment.CraftEquipmentAsync(race.Steam, command, Now, default)).Status == ItemRepositoryCommitStatus.Committed; }
            catch (EquipmentProgressionRuleException error) when (error.Code == "item_revision_conflict") { return false; }
        }
        bool[] winners = await Task.WhenAll(Task.Run(() => Attempt(a)), Task.Run(() => Attempt(b)));
        Require(winners.Count(x => x) == 1 && (await items.FindByOwnerAsync(race.Owner, false, 100, default)).Count(x => x.DefinitionName == Target) == 1
            && await items.TryGetCommitResultAsync(winners[0] ? b.RequestId : a.RequestId, default) is null, "Concurrent craft duplicated output or stored losing receipt.");
        groups.Add("concurrent_same_tokens_exactly_one_output_and_no_loser_receipt");

        foreach (int count in new[] { 8, 9 })
        {
            CraftFixture full = await NewFixture(game, items, count);
            await Fill(items, full, 1);
            var output = (await equipment.CraftEquipmentAsync(full.Steam, Craft(full), Now, default)).Records.Single(x => x.DefinitionName == Target);
            Require(output.Location.ContainerType == (count == 8 ? ItemContainerType.Inventory : ItemContainerType.Mail)
                && (count != 8 || output.Location.SlotIndex == 0), "Delivery ignored consumed-token free slot/full bag.");
        }
        groups.Add("full_inventory_reuses_consumed_stack_slot_or_falls_back_to_mail");

        foreach (int tokenCount in new[] { 998, 999 })
        {
            CraftFixture full = await NewFixture(game, items, tokenCount);
            await Fill(items, full, 1);
            ItemRecord gear = Make(full.Owner, Target, 1, 1, true) with { Location = new(ItemContainerType.Equipment, full.Owner.OwnerId, 1) };
            gear = (await Seed(items, full.Owner, [gear]))[0];
            var result = await equipment.DismantleEquipmentAsync(full.Steam, Dismantle(full, gear), Now, default);
            ItemRecord returned = result.Records.Single(x => x.LifecycleState == ItemLifecycleState.Active);
            Require(tokenCount == 998 ? returned.ItemId == full.Token.ItemId && returned.State.Quantity == 999
                : returned.ItemId != full.Token.ItemId && returned.State.Quantity == 1 && returned.Location.ContainerType == ItemContainerType.Mail, "Token merge exceeded 999 or lost full-bag return.");
        }
        groups.Add("equipped_dismantle_merges_token_up_to_999_then_mail_overflow");

        CraftFixture invalid = await NewFixture(game, items, 7);
        var insufficient = Craft(invalid);
        await Reject(() => equipment.CraftEquipmentAsync(invalid.Steam, insufficient, Now, default), "insufficient_tokens");
        await Reject(() => equipment.CraftEquipmentAsync("foreign", insufficient, Now, default), "character_not_owned");
        CraftFixture foreign = await NewFixture(game, items);
        await Reject(() => equipment.CraftEquipmentAsync(invalid.Steam, Craft(invalid, foreign.Token), Now, default), "item_not_found");
        Require((await items.FindAsync(invalid.Token.ItemId, default))!.State.Quantity == 7 && await items.TryGetCommitResultAsync(insufficient.RequestId, default) is null, "Refusal left cost/receipt.");
        groups.Add("insufficient_and_foreign_requests_preserve_cost_and_receipts");
    }

    private static void DomainRejections(List<string> groups)
    {
        Guid character = Guid.NewGuid(); ItemOwnerRef owner = new(ItemOwnerType.Character, character.ToString("D"));
        ItemRecord token = Make(owner, FirstSliceEquipmentDefinitions.TokenDefinitionName, 0, 8) with { Revision = 1 };
        var craft = new FirstSliceEquipmentCraftCommand(Guid.NewGuid(), character, 1, Target, 1, [new(token.ItemId, 1)]);
        void RefuseCraft(ItemRecord record, string code, FirstSliceEquipmentCraftCommand? command = null) => Refuse(() => FirstSliceEquipmentCraftingPlanner.PlanCraft(command ?? craft, new Dictionary<Guid, ItemRecord> { [record.ItemId] = record }, Guid.NewGuid(), Now), code);
        RefuseCraft(token with { Metadata = token.Metadata with { IsLocked = true } }, "item_locked");
        RefuseCraft(token with { Metadata = token.Metadata with { ExpiresAtUtc = Now } }, "item_expired");
        RefuseCraft(token with { Metadata = token.Metadata with { BindState = ItemBindState.AccountBound } }, "item_not_character_bound");
        RefuseCraft(token with { Revision = 2 }, "item_revision_conflict");
        RefuseCraft(token with { Location = token.Location with { ContainerType = ItemContainerType.Mail } }, "invalid_token_stack");
        RefuseCraft(token with { Location = token.Location with { ContainerType = ItemContainerType.CharacterStorage } }, "invalid_token_stack");
        RefuseCraft(token with { Location = token.Location with { SlotIndex = 80 } }, "invalid_token_stack");
        RefuseCraft(token with { DefinitionName = FirstSliceEquipmentDefinitions.OreDefinitionName }, "unsupported_token_definition");
        RefuseCraft(token, "unsupported_craft_target", craft with { TargetDefinitionName = FirstSliceEquipmentDefinitions.All[0].DefinitionName });
        RefuseCraft(token, "unsupported_craft_target", craft with { TargetDefinitionVersion = 2 });
        RefuseCraft(token, "duplicate_item_id", craft with { TokenStacks = [new(token.ItemId, 1), new(token.ItemId, 1)] });
        ItemRecord gear = Make(owner, Target, 3, 1, true) with { Revision = 1 };
        void RefuseGear(ItemRecord record, string code, int level = 0) => Refuse(() => FirstSliceEquipmentCraftingPlanner.PlanDismantle(
            new(Guid.NewGuid(), character, 1, new(record.ItemId, 1, level)), new Dictionary<Guid, ItemRecord> { [record.ItemId] = record }, Guid.NewGuid(), Now), code);
        RefuseGear(gear with { DefinitionName = FirstSliceEquipmentDefinitions.All[1].DefinitionName }, "starter_equipment_not_dismantlable");
        RefuseGear(gear with { State = gear.State with { EquipmentProgression = FirstSliceEquipmentProgression.Create() with { Level = 1 } } }, "enhanced_equipment_not_dismantlable", 1);
        RefuseGear(gear with { State = gear.State with { EquipmentProgression = null } }, "equipment_not_initialized");
        RefuseGear(gear with { DefinitionName = StarterEquipment.DefinitionName }, "unsupported_equipment_definition");
        RefuseGear(gear with { Location = gear.Location with { ContainerType = ItemContainerType.CharacterStorage } }, "invalid_equipment_location");
        RefuseGear(gear with { Metadata = gear.Metadata with { IsLocked = true } }, "item_locked");
        groups.Add("17_planner_refusals_locked_expired_bound_revision_storage_legacy_starter_enhanced");
    }

    private static async Task Rollback(NpgsqlDataSource data, PostgresGameRepository game, PostgresItemRepository items, List<string> groups)
    {
        CraftFixture f = await NewFixture(game, items);
        var command = Craft(f);
        await using NpgsqlConnection connection = await data.OpenConnectionAsync();
        // Owned ephemeral database only. Fail AFTER item writes, at receipt insertion.
        await using (NpgsqlCommand install = new($"""
            CREATE FUNCTION fail_craft_receipt() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN
              IF NEW.request_id = '{command.RequestId:D}'::uuid THEN RAISE EXCEPTION 'owned receipt failure'; END IF;
              RETURN NEW; END $$;
            CREATE TRIGGER fail_craft_receipt BEFORE INSERT ON item_transaction_receipts FOR EACH ROW EXECUTE FUNCTION fail_craft_receipt();
            """, connection)) await install.ExecuteNonQueryAsync();
        try
        {
            try { var failure = await game.CraftEquipmentAsync(f.Steam, command, Now, default); Require(failure.Status != ItemRepositoryCommitStatus.Committed, "Injected failure committed."); }
            catch (PostgresException) { }
            Require((await items.FindAsync(f.Token.ItemId, default))!.State.Quantity == 8
                && (await items.FindByOwnerAsync(f.Owner, false, 100, default)).All(x => x.DefinitionName != Target)
                && await items.TryGetCommitResultAsync(command.RequestId, default) is null, "Receipt failure did not roll back cost/output.");
        }
        finally
        {
            await using NpgsqlCommand remove = new("DROP TRIGGER fail_craft_receipt ON item_transaction_receipts; DROP FUNCTION fail_craft_receipt();", connection);
            await remove.ExecuteNonQueryAsync();
        }
        Require((await game.CraftEquipmentAsync(f.Steam, command, Now, default)).Status == ItemRepositoryCommitStatus.Committed, "Failed request cannot retry.");
        groups.Add("postgres_receipt_failure_rolls_back_tokens_output_and_retries_atomically");
    }

    internal static async Task<CraftFixture> NewFixture(IGameRepository game, IItemRepository items, int tokens = 8)
    {
        string suffix = Guid.NewGuid().ToString("N")[..10]; string steam = "craft-" + suffix;
        await game.UpsertAccountAsync(new(steam, steam, false, false), Now, default);
        GameCharacter character = await game.CreateCharacterAsync(steam, "Craft" + suffix, Now, default);
        ItemOwnerRef owner = new(ItemOwnerType.Character, character.CharacterId.ToString("D"));
        ItemRecord token = (await Seed(items, owner, [Make(owner, FirstSliceEquipmentDefinitions.TokenDefinitionName, 0, tokens)]))[0];
        return new(steam, character, owner, token);
    }
    internal static ItemRecord Make(ItemOwnerRef owner, string name, int slot, int count, bool gear = false) => new("RPGItemDefinition", name, 1, owner,
        new(ItemContainerType.Inventory, owner.OwnerId, slot), new(Guid.NewGuid(), 0, count, [], [], gear ? FirstSliceEquipmentProgression.Create() : null),
        0, ItemLifecycleState.Active, new(ItemBindState.CharacterBound, new(0, 0), null, "CraftFixture", false));
    internal static FirstSliceEquipmentCraftCommand Craft(CraftFixture f, ItemRecord? token = null) => new(Guid.NewGuid(), f.Character.CharacterId, 1,
        Target, 1, [new((token ?? f.Token).ItemId, (token ?? f.Token).Revision)]);
    internal static FirstSliceEquipmentDismantleCommand Dismantle(CraftFixture f, ItemRecord gear) => new(Guid.NewGuid(), f.Character.CharacterId, 1, Expect(gear));
    internal static async Task<IReadOnlyList<ItemRecord>> Seed(IItemRepository items, ItemOwnerRef owner, IReadOnlyList<ItemRecord> records)
    {
        var result = await items.CommitAsync(new(Guid.NewGuid(), "CraftFixture", "owned-server-fixture", owner, 0, records.Select(x => new ItemRecordMutation(0, x)).ToArray()), Now, default);
        Require(result.Status == ItemRepositoryCommitStatus.Committed, "Craft fixture seed failed: " + result.Status); return result.Records;
    }
    private static async Task Fill(IItemRepository items, CraftFixture f, int start)
    {
        foreach (int[] slots in Enumerable.Range(start, 80 - start).Chunk(16))
            await Seed(items, f.Owner, slots.Select(slot => Make(f.Owner, "GameItem.CraftFixture.Filler", slot, 1)).ToArray());
    }
    private static void Refuse(Action action, string code)
    {
        try { action(); } catch (EquipmentProgressionRuleException error) { Require(error.Code == code, "Expected " + code + ", got " + error.Code); return; }
        throw new InvalidOperationException("Unsafe planner accepted " + code);
    }
    private static void Require(bool condition, string message) => EquipmentLobbyCases.Require(condition, message);
}
