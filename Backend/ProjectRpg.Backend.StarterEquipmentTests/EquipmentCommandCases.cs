using System.Text.Json;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using Planner = ProjectRpg.Backend.Domain.FirstSliceEquipmentCommandPlanner;

internal static class EquipmentCommandCases
{
    private static readonly Guid Character = Guid.Parse("40000000-0000-0000-0000-000000000001");
    private static readonly ItemOwnerRef Owner = new(ItemOwnerType.Character, Character.ToString("D"));
    private static readonly DateTimeOffset Now = new(2026, 10, 31, 12, 0, 0, TimeSpan.Zero);

    public static async Task RunAsync()
    {
        List<string> groups = [];
        string manifestPath = Environment.GetEnvironmentVariable("RPG_EQUIPMENT_PACKAGE_MANIFEST")
            ?? throw new InvalidOperationException("Set RPG_EQUIPMENT_PACKAGE_MANIFEST to the actual authored manifest.");
        using JsonDocument manifest = JsonDocument.Parse(File.ReadAllText(manifestPath));
        JsonElement[] assets = manifest.RootElement.GetProperty("assets").EnumerateArray().ToArray();
        Require(assets.Length == 7 && FirstSliceEquipmentDefinitions.All.Count == 7, "Definition set differs.");
        foreach (JsonElement asset in assets)
        {
            JsonElement properties = asset.GetProperty("properties");
            string id = properties.GetProperty("expected_primary_asset_id").GetString()!;
            string[] parts = id.Split(':');
            Require(FirstSliceEquipmentDefinitions.TryResolve(parts[0], parts[1],
                properties.GetProperty("definition_version").GetInt32(), out FirstSliceEquipmentDefinition entry)
                && entry.Model.ToString() == asset.GetProperty("model").GetString()
                && entry.EquipmentSlotIndex == properties.GetProperty("slot_index").GetInt32(), "Authored definition and server disagree.");
            foreach ((string type, string name, int version) in new[]
            {
                (parts[0].ToLowerInvariant(), parts[1], 1), (parts[0], parts[1].ToLowerInvariant(), 1),
                (parts[0], parts[1], 0), (parts[0], parts[1], 2), (parts[0], parts[1] + ".Forged", 1)
            }) Require(!FirstSliceEquipmentDefinitions.TryResolve(type, name, version, out _), "Identity was inferred or version substituted.");
        }
        Require(!FirstSliceEquipmentDefinitions.TryResolve(StarterEquipment.DefinitionType, StarterEquipment.DefinitionName, 1, out _),
            "Legacy starter was silently migrated.");
        using JsonDocument materials = JsonDocument.Parse(File.ReadAllText(
            Environment.GetEnvironmentVariable("RPG_MATERIAL_PACKAGE_MANIFEST")
                ?? throw new InvalidOperationException("Set RPG_MATERIAL_PACKAGE_MANIFEST.")));
        JsonElement oreProperties = materials.RootElement.GetProperty("assets").EnumerateArray()
            .Select(x => x.GetProperty("properties")).Single(x => x.TryGetProperty("expected_primary_asset_id", out JsonElement identity)
                && identity.GetString() == FirstSliceEquipmentDefinitions.DefinitionType + ":" + FirstSliceEquipmentDefinitions.OreDefinitionName);
        Require(oreProperties.GetProperty("definition_version").GetInt32() == FirstSliceEquipmentDefinitions.DefinitionVersion
            && oreProperties.GetProperty("max_stack_size").GetInt32() == FirstSliceEquipmentDefinitions.OreMaximumStackSize,
            "Server cost definition disagrees with the actual material package.");
        groups.Add("exact_seven_authored_ids_versions_slots_and_legacy_refusal");

        int plans = 0;
        foreach (FirstSliceEquipmentDefinition definition in FirstSliceEquipmentDefinitions.All)
        for (int level = 0; level < 3; level++)
        {
            ItemRecord equipment = Equipment(definition.Model, level);
            ItemRecord first = Ore(1, 10, Guid.Parse("10000000-0000-0000-0000-000000000001"));
            ItemRecord second = Ore(20, 11, Guid.Parse("20000000-0000-0000-0000-000000000001"));
            Dictionary<Guid, ItemRecord> records = Records(equipment, first, second);
            string before = JsonSerializer.Serialize(records);
            FirstSliceEquipmentUpgradeCommand command = Upgrade(equipment, second, first);
            string originalOrder = JsonSerializer.Serialize(command);
            ItemRepositoryCommitRequest plan = Planner.PlanUpgrade(command, records, Now);
            int cost = new[] { 2, 4, 6 }[level];
            Require(plan.Operation == Planner.UpgradeOperation && plan.AffectedQuantity == cost && plan.Mutations.Count == 3,
                "Upgrade did not plan its complete deterministic cost.");
            ItemRecord next = plan.Mutations[0].NewRecord;
            Require(next.State.EquipmentProgression!.Level == level + 1, "Equipment did not advance one level.");
            SameExceptGrowth(equipment, next);
            Require(plan.Mutations[1].NewRecord.ItemId == first.ItemId
                && plan.Mutations[1].NewRecord.State.Quantity == 0
                && plan.Mutations[1].NewRecord.LifecycleState == ItemLifecycleState.Consumed
                && plan.Mutations[1].NewRecord.Location == new ItemLocation(ItemContainerType.Terminal, "", -1)
                && plan.Mutations[1].NewRecord.Metadata == first.Metadata,
                "First sorted ore stack must become an unchanged-metadata tombstone.");
            ItemRecord leftover = plan.Mutations[2].NewRecord;
            Require(leftover.ItemId == second.ItemId && leftover.State.Quantity == 21 - cost
                && leftover.Location == second.Location && leftover.Metadata == second.Metadata,
                "Partial ore remainder or metadata was lost.");
            Require(plan.Mutations.All(m => m.ExpectedRevision == m.NewRecord.Revision)
                && JsonSerializer.Serialize(records) == before && JsonSerializer.Serialize(command) == originalOrder,
                "Planning changed an input or advanced CAS revisions early.");
            plans++;
        }
        groups.Add("twenty_one_record_upgrade_plans_sorted_cost_tombstone_and_preservation");

        ItemRecord weapon = Equipment(FirstSliceEquipmentModel.StarterGreatSword, 0);
        ItemRecord ore = Ore(10, 10);
        Dictionary<Guid, ItemRecord> valid = Records(weapon, ore);
        FirstSliceEquipmentUpgradeCommand basic = Upgrade(weapon, ore);
        foreach (Func<ItemRecord, ItemRecord> change in new Func<ItemRecord, ItemRecord>[]
        {
            x => x with { DefinitionName = StarterEquipment.DefinitionName },
            x => x with { DefinitionVersion = 2 },
            x => x with { Owner = x.Owner with { OwnerId = Guid.NewGuid().ToString("D") } },
            x => x with { Location = x.Location with { ContainerType = ItemContainerType.Mail } },
            x => x with { Location = x.Location with { ContainerType = ItemContainerType.CharacterStorage } },
            x => x with { Location = x.Location with { SlotIndex = 1 } },
            x => x with { Metadata = x.Metadata with { IsLocked = true } },
            x => x with { Metadata = x.Metadata with { BindState = ItemBindState.AccountBound } },
            x => x with { Metadata = x.Metadata with { ExpiresAtUtc = Now } },
            x => x with { State = x.State with { Quantity = 2 } },
            x => x with { State = x.State with { Quantity = 0 }, LifecycleState = ItemLifecycleState.Consumed,
                Location = new(ItemContainerType.Terminal, "", -1) },
            x => x with { State = x.State with { EquipmentProgression = null } },
            x => x with { State = x.State with { EquipmentProgression = FirstSliceEquipmentProgression.Create() with { Level = 4 } } }
        }) Reject(() => Planner.PlanUpgrade(basic, Records(change(weapon), ore), Now));
        foreach (Func<ItemRecord, ItemRecord> change in new Func<ItemRecord, ItemRecord>[]
        {
            x => x with { DefinitionName = "GameItem.Material.Progression.Token" },
            x => x with { DefinitionVersion = 2 },
            x => x with { Owner = x.Owner with { Type = ItemOwnerType.Account } },
            x => x with { Location = x.Location with { ContainerType = ItemContainerType.Mail } },
            x => x with { Location = x.Location with { ContainerType = ItemContainerType.Equipment } },
            x => x with { Metadata = x.Metadata with { IsLocked = true } },
            x => x with { Metadata = x.Metadata with { BindState = ItemBindState.Unbound } },
            x => x with { Metadata = x.Metadata with { ExpiresAtUtc = Now.AddTicks(-1) } },
            x => x with { State = x.State with { Quantity = 1000 } },
            x => x with { State = x.State with { Quantity = 0 }, LifecycleState = ItemLifecycleState.Consumed,
                Location = new(ItemContainerType.Terminal, "", -1) },
            x => x with { State = x.State with { Quantity = 1, EquipmentProgression = FirstSliceEquipmentProgression.Create() } }
        }) Reject(() => Planner.PlanUpgrade(basic, Records(weapon, change(ore)), Now));
        foreach (FirstSliceEquipmentUpgradeCommand invalid in new[]
        {
            basic with { RequestId = Guid.Empty }, basic with { CharacterId = Guid.Empty }, basic with { RuleVersion = 2 },
            basic with { Equipment = basic.Equipment with { ItemId = Guid.Empty } },
            basic with { Equipment = basic.Equipment with { Revision = 0 } },
            basic with { Equipment = basic.Equipment with { Revision = long.MaxValue } },
            basic with { Equipment = basic.Equipment with { Revision = 2 } },
            basic with { Equipment = basic.Equipment with { Level = 1 } },
            basic with { OreStacks = [] }, basic with { OreStacks = [new(ore.ItemId, 1), new(ore.ItemId, 1)] },
            basic with { OreStacks = [new(weapon.ItemId, 1)] }, basic with { OreStacks = [new(Guid.NewGuid(), 1)] },
            basic with { OreStacks = [new(ore.ItemId, long.MaxValue)] }, basic with { OreStacks = [new(ore.ItemId, 2)] },
            basic with { OreStacks = Enumerable.Range(0, 16).Select(_ => new EquipmentOreExpectation(Guid.NewGuid(), 1)).ToArray() }
        }) Reject(() => Planner.PlanUpgrade(invalid, valid, Now));
        Reject(() => Planner.PlanUpgrade(basic, Records(weapon, ore with { State = ore.State with { Quantity = 1 } }), Now), "insufficient_ore");
        ItemRecord maximum = weapon with { State = weapon.State with { EquipmentProgression = FirstSliceEquipmentProgression.Create() with { Level = 3 } } };
        Reject(() => Planner.PlanUpgrade(Upgrade(maximum, ore), Records(maximum, ore), Now), "equipment_at_maximum_level");
        groups.Add("record_authority_expiry_lock_bound_state_revision_cost_and_bounds_refusal");

        ItemRecord source = Equipment(FirstSliceEquipmentModel.StarterGreatSword, 3);
        ItemRecord target = Equipment(FirstSliceEquipmentModel.EnduranceGreatSword, 0, inventorySlot: 7);
        FirstSliceEquipmentTransferCommand transfer = Transfer(source, target);
        ItemRepositoryCommitRequest transferPlan = Planner.PlanTransfer(transfer, Records(source, target), Now);
        Require(transferPlan.Mutations.Count == 2 && transferPlan.AffectedQuantity == 0
            && transferPlan.Mutations[0].NewRecord.State.EquipmentProgression!.Level == 0
            && transferPlan.Mutations[1].NewRecord.State.EquipmentProgression!.Level == 3, "Transfer did not move the whole level.");
        SameExceptGrowth(source, transferPlan.Mutations[0].NewRecord);
        SameExceptGrowth(target, transferPlan.Mutations[1].NewRecord);
        Reject(() => Planner.PlanTransfer(transfer with { Target = transfer.Source }, Records(source, target), Now), "duplicate_item_id");
        ItemRecord chest = Equipment(FirstSliceEquipmentModel.TargetChest, 0);
        Reject(() => Planner.PlanTransfer(Transfer(source, chest), Records(source, chest), Now), "equipment_slot_mismatch");
        ItemRecord enhanced = target with { State = target.State with { EquipmentProgression = FirstSliceEquipmentProgression.Create() with { Level = 1 } } };
        Reject(() => Planner.PlanTransfer(Transfer(source, enhanced), Records(source, enhanced), Now), "transfer_target_already_enhanced");
        ItemRecord zero = source with { State = source.State with { EquipmentProgression = FirstSliceEquipmentProgression.Create() } };
        Reject(() => Planner.PlanTransfer(Transfer(zero, target), Records(zero, target), Now), "transfer_source_not_enhanced");
        groups.Add("whole_level_two_record_transfer_and_identity_preservation");

        ItemRecord ore2 = Ore(4, 11);
        FirstSliceEquipmentUpgradeCommand unordered = Upgrade(weapon, ore2, ore);
        string fingerprint = Planner.Fingerprint(unordered);
        Require(fingerprint == Planner.Fingerprint(unordered with { OreStacks = unordered.OreStacks.Reverse().ToArray() }),
            "Cost ordering changed the command identity.");
        foreach (FirstSliceEquipmentUpgradeCommand different in new[]
        {
            unordered with { RequestId = Guid.NewGuid() }, unordered with { CharacterId = Guid.NewGuid() },
            unordered with { Equipment = unordered.Equipment with { Level = 1 } },
            unordered with { Equipment = unordered.Equipment with { Revision = 2 } },
            unordered with { Equipment = unordered.Equipment with { ItemId = Guid.NewGuid() } },
            unordered with { OreStacks = [new(ore.ItemId, 2), new(ore2.ItemId, 1)] },
            unordered with { OreStacks = [new(Guid.NewGuid(), 1), new(ore2.ItemId, 1)] },
            unordered with { OreStacks = [new(ore.ItemId, 1)] }
        }) Require(fingerprint != Planner.Fingerprint(different), "Changed command reused its fingerprint.");
        Require(Planner.Fingerprint(transfer) != Planner.Fingerprint(transfer with { Target = transfer.Target with { Revision = 2 } })
            && Planner.Fingerprint(transfer) != Planner.Fingerprint(transfer with { Source = transfer.Target, Target = transfer.Source }),
            "Transfer identity lost direction or a revision.");
        groups.Add("canonical_request_fingerprints_without_current_records");

        await MemoryUpgradeAsync();
        groups.Add("memory_actual_cost_replay_and_shared_ore_cas_atomicity");
        await MemoryTransferAsync();
        groups.Add("memory_transfer_target_cas_and_old_receipt_preserves_latest_record");
        Console.WriteLine(JsonSerializer.Serialize(new { passed = true, scope = "server domain planning and existing Memory commit only", plans, groups }));
    }

    private static async Task MemoryUpgradeAsync()
    {
        InMemoryItemRepository repo = new(new InMemoryTransactionGate());
        ItemTransactionService service = new(repo, TimeProvider.System);
        ItemRecord first = Equipment(FirstSliceEquipmentModel.StarterGreatSword, 0);
        ItemRecord second = Equipment(FirstSliceEquipmentModel.StarterGloves, 0);
        ItemRecord ore = Ore(3, 10);
        await SeedAsync(service, first, second, ore);
        var inputs = Records(first, second, ore);
        ItemRepositoryCommitRequest winner = Planner.PlanUpgrade(Upgrade(first, ore), inputs, Now);
        ItemRepositoryCommitRequest loser = Planner.PlanUpgrade(Upgrade(second, ore), inputs, Now);
        Require((await service.CommitAsync(winner, default)).Status == ItemRepositoryCommitStatus.Committed, "Upgrade commit failed.");
        Require((await service.CommitAsync(loser, default)).Status == ItemRepositoryCommitStatus.RevisionConflict, "Shared ore was spent twice.");
        Require(JsonSerializer.Serialize(await repo.FindAsync(second.ItemId, default)) == JsonSerializer.Serialize(second)
            && (await repo.FindAsync(ore.ItemId, default))!.State.Quantity == 1
            && await repo.TryGetCommitResultAsync(loser.RequestId, default) is null, "Losing plan partially wrote equipment or a receipt.");
        Require((await service.CommitAsync(winner, default)).Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && (await repo.FindAsync(ore.ItemId, default))!.State.Quantity == 1, "Replay spent ore twice.");
        Require((await service.CommitAsync(winner with { CommandFingerprint = "different-command" }, default)).Status
            == ItemRepositoryCommitStatus.IdempotencyConflict, "Changed request reused the receipt.");

        ItemRecord upgraded = (await repo.FindAsync(first.ItemId, default))!;
        ItemRecord extra = Ore(10, 11);
        await SeedAsync(service, extra);
        ItemRepositoryCommitRequest next = Planner.PlanUpgrade(Upgrade(upgraded, extra), Records(upgraded, extra), Now);
        Require((await service.CommitAsync(next, default)).Status == ItemRepositoryCommitStatus.Committed
            && (await repo.FindAsync(first.ItemId, default))!.State.EquipmentProgression!.Level == 2
            && (await repo.FindAsync(extra.ItemId, default))!.State.Quantity == 6, "Persisted next step failed to charge four ore.");

        ItemRecord terminalOre = Ore(2, 12);
        ItemRecord chest = Equipment(FirstSliceEquipmentModel.StarterChest, 0);
        await SeedAsync(service, terminalOre, chest);
        ItemRepositoryCommitRequest terminalPlan = Planner.PlanUpgrade(Upgrade(chest, terminalOre), Records(chest, terminalOre), Now);
        Require((await service.CommitAsync(terminalPlan, default)).Status == ItemRepositoryCommitStatus.Committed,
            "Full cost stack commit failed.");
        ItemRecord terminal = (await repo.FindAsync(terminalOre.ItemId, default))!;
        Require(terminal.LifecycleState == ItemLifecycleState.Consumed && terminal.State.Quantity == 0
            && terminal.Location.ContainerType == ItemContainerType.Terminal, "Consumed ore did not persist as terminal.");
    }

    private static async Task MemoryTransferAsync()
    {
        InMemoryItemRepository repo = new(new InMemoryTransactionGate());
        ItemTransactionService service = new(repo, TimeProvider.System);
        ItemRecord source = Equipment(FirstSliceEquipmentModel.StarterGreatSword, 2);
        ItemRecord target = Equipment(FirstSliceEquipmentModel.CirculationGreatSword, 0, inventorySlot: 4);
        await SeedAsync(service, source, target);
        ItemRepositoryCommitRequest stale = Planner.PlanTransfer(Transfer(source, target), Records(source, target), Now);
        ItemRepositoryCommitRequest move = new(Guid.NewGuid(), "MoveItem", "move-before-transfer", Owner, 0,
            [new(target.Revision, target with { Location = target.Location with { SlotIndex = 5 } })]);
        Require((await service.CommitAsync(move, default)).Status == ItemRepositoryCommitStatus.Committed, "Target move failed.");
        Require((await service.CommitAsync(stale, default)).Status == ItemRepositoryCommitStatus.RevisionConflict
            && JsonSerializer.Serialize(await repo.FindAsync(source.ItemId, default)) == JsonSerializer.Serialize(source)
            && await repo.TryGetCommitResultAsync(stale.RequestId, default) is null, "Stale target reset source or stored success.");
        target = (await repo.FindAsync(target.ItemId, default))!;
        ItemRepositoryCommitRequest fresh = Planner.PlanTransfer(Transfer(source, target), Records(source, target), Now);
        ItemRepositoryCommitResult committed = await service.CommitAsync(fresh, default);
        Require(committed.Status == ItemRepositoryCommitStatus.Committed, "Fresh transfer failed.");
        ItemRecord moved = committed.Records[1];
        move = move with { RequestId = Guid.NewGuid(), CommandFingerprint = "move-after-transfer",
            Mutations = [new(moved.Revision, moved with { Location = moved.Location with { SlotIndex = 6 } })] };
        Require((await service.CommitAsync(move, default)).Status == ItemRepositoryCommitStatus.Committed, "Post-transfer move failed.");
        ItemRepositoryCommitResult replay = await service.CommitAsync(fresh, default);
        Require(replay.Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && replay.Records[1].Location.SlotIndex == 5
            && (await repo.FindAsync(target.ItemId, default))!.Location.SlotIndex == 6
            && (await repo.FindAsync(source.ItemId, default))!.State.EquipmentProgression!.Level == 0,
            "Old receipt overwrote today's location or transferred again.");
    }

    private static async Task SeedAsync(ItemTransactionService service, params ItemRecord[] records)
    {
        ItemRepositoryCommitRequest create = new(Guid.NewGuid(), "EquipmentCommandFixture", "fixture", Owner, 0,
            records.Select(item => new ItemRecordMutation(0, item with { Revision = 0 })).ToArray());
        Require((await service.CommitAsync(create, default)).Status == ItemRepositoryCommitStatus.Committed, "Fixture seed failed.");
    }

    private static ItemRecord Equipment(FirstSliceEquipmentModel model, int level, int? inventorySlot = null)
    {
        FirstSliceEquipmentDefinition definition = FirstSliceEquipmentDefinitions.All.Single(x => x.Model == model);
        ItemRecord item = Base(definition.DefinitionName, Guid.NewGuid(), inventorySlot ?? definition.EquipmentSlotIndex, 1);
        return item with
        {
            Location = new(inventorySlot is null ? ItemContainerType.Equipment : ItemContainerType.Inventory,
                Owner.OwnerId, inventorySlot ?? definition.EquipmentSlotIndex),
            State = item.State with { EquipmentProgression = FirstSliceEquipmentProgression.Create() with { Level = level } }
        };
    }

    private static ItemRecord Ore(int quantity, int slot, Guid? id = null) =>
        Base(FirstSliceEquipmentDefinitions.OreDefinitionName, id ?? Guid.NewGuid(), slot, quantity);

    private static ItemRecord Base(string name, Guid id, int slot, int quantity) => new(
        "RPGItemDefinition", name, 1, Owner, new(ItemContainerType.Inventory, Owner.OwnerId, slot),
        new(id, 727, quantity, ["Item.Test.Preserve"], [new("Stat.Test.Preserve", 9.25)]), 1, ItemLifecycleState.Active,
        new(ItemBindState.CharacterBound, new(4, 10), Now.AddYears(1), "CommandFixture", false));

    private static Dictionary<Guid, ItemRecord> Records(params ItemRecord[] records) => records.ToDictionary(x => x.ItemId);
    private static EquipmentItemExpectation Expect(ItemRecord item) => new(item.ItemId, item.Revision, item.State.EquipmentProgression!.Level);
    private static FirstSliceEquipmentUpgradeCommand Upgrade(ItemRecord item, params ItemRecord[] ore) =>
        new(Guid.NewGuid(), Character, 1, Expect(item), ore.Select(x => new EquipmentOreExpectation(x.ItemId, x.Revision)).ToArray());
    private static FirstSliceEquipmentTransferCommand Transfer(ItemRecord source, ItemRecord target) =>
        new(Guid.NewGuid(), Character, 1, Expect(source), Expect(target));

    private static void SameExceptGrowth(ItemRecord before, ItemRecord after) => Require(
        JsonSerializer.Serialize(before) == JsonSerializer.Serialize(after with
        { State = after.State with { EquipmentProgression = before.State.EquipmentProgression } }),
        "Planning changed identity, location, metadata, revision, or unrelated instance data.");

    private static void Reject(Action action, string? code = null)
    {
        try { action(); }
        catch (EquipmentProgressionRuleException error)
        {
            Require(code is null || error.Code == code, "Unexpected refusal: " + error.Code);
            return;
        }
        throw new InvalidOperationException("Unsafe plan accepted.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
