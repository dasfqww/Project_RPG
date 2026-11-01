using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain;

public sealed record EquipmentItemExpectation(Guid ItemId, long Revision, int Level);
public sealed record EquipmentOreExpectation(Guid ItemId, long Revision);
public sealed record FirstSliceEquipmentUpgradeCommand(Guid RequestId, Guid CharacterId, int RuleVersion,
    EquipmentItemExpectation Equipment, IReadOnlyList<EquipmentOreExpectation> OreStacks);
public sealed record FirstSliceEquipmentTransferCommand(Guid RequestId, Guid CharacterId, int RuleVersion,
    EquipmentItemExpectation Source, EquipmentItemExpectation Target);

/// <summary>
/// Plans from SERVER-READ records. Does not authenticate, check dungeon leases, or commit.
/// The future handler must recover an exact receipt BEFORE planning, and serialize new
/// plans/commits with admission and item writes in one Memory gate / PG transaction.
/// Never expose the resulting generic commit or accept client-supplied records.
/// </summary>
public static class FirstSliceEquipmentCommandPlanner
{
    public const string UpgradeOperation = "FirstSliceEquipmentUpgrade";
    public const string TransferOperation = "FirstSliceEquipmentTransfer";
    public const int MaximumOreStacks = ItemRecordRules.MaximumMutationCount - 1;

    // Available without today's records, so a handler can identify an old receipt
    // even after the item moved, advanced, expired, or acquired a dungeon lease.
    public static string Fingerprint(FirstSliceEquipmentUpgradeCommand command)
        => Hash(UpgradeOperation, Normalize(command));

    public static string Fingerprint(FirstSliceEquipmentTransferCommand command)
    {
        Validate(command);
        return Hash(TransferOperation, command);
    }

    public static ItemRepositoryCommitRequest PlanUpgrade(FirstSliceEquipmentUpgradeCommand command,
        IReadOnlyDictionary<Guid, ItemRecord> records, DateTimeOffset now)
    {
        command = Normalize(command);
        ItemOwnerRef owner = Owner(command.CharacterId);
        (ItemRecord equipment, FirstSliceEquipmentDefinition definition) =
            ReadEquipment(command.Equipment, owner, records, now);
        FirstSliceEquipmentUpgradeStep step = FirstSliceEquipmentProgression.Upgrade(
            definition.Model, equipment.State.EquipmentProgression!);
        List<ItemRecord> ore = [];
        foreach (EquipmentOreExpectation expected in command.OreStacks)
        {
            ItemRecord stack = ReadActive(expected.ItemId, expected.Revision, owner, records, now);
            Require(stack.DefinitionType == FirstSliceEquipmentDefinitions.DefinitionType
                && stack.DefinitionName == FirstSliceEquipmentDefinitions.OreDefinitionName
                && stack.DefinitionVersion == FirstSliceEquipmentDefinitions.DefinitionVersion,
                "unsupported_ore_definition");
            Require(stack.Location.ContainerType == ItemContainerType.Inventory
                && stack.State.Quantity <= FirstSliceEquipmentDefinitions.OreMaximumStackSize
                && stack.State.EquipmentProgression is null, "invalid_ore_stack");
            ore.Add(stack);
        }
        Require(ore.Sum(stack => (long)stack.State.Quantity) >= step.OreCost, "insufficient_ore");
        List<ItemRecordMutation> mutations = [Mutation(equipment with
        {
            State = equipment.State with { EquipmentProgression = step.NextState }
        })];
        int remaining = step.OreCost;
        foreach (ItemRecord stack in ore)
        {
            if (remaining == 0) break;
            int used = Math.Min(remaining, stack.State.Quantity);
            int quantity = stack.State.Quantity - used;
            ItemRecord next = stack with { State = stack.State with { Quantity = quantity } };
            if (quantity == 0) next = next with
            {
                Location = new(ItemContainerType.Terminal, string.Empty, -1),
                LifecycleState = ItemLifecycleState.Consumed
            };
            mutations.Add(Mutation(next));
            remaining -= used;
        }
        return Commit(command.RequestId, UpgradeOperation, Fingerprint(command), owner, step.OreCost, mutations);
    }

    public static ItemRepositoryCommitRequest PlanTransfer(FirstSliceEquipmentTransferCommand command,
        IReadOnlyDictionary<Guid, ItemRecord> records, DateTimeOffset now)
    {
        Validate(command);
        ItemOwnerRef owner = Owner(command.CharacterId);
        (ItemRecord source, FirstSliceEquipmentDefinition sourceDefinition) = ReadEquipment(command.Source, owner, records, now);
        (ItemRecord target, FirstSliceEquipmentDefinition targetDefinition) = ReadEquipment(command.Target, owner, records, now);
        FirstSliceEquipmentTransferStep step = FirstSliceEquipmentProgression.Transfer(sourceDefinition.Model,
            source.State.EquipmentProgression!, targetDefinition.Model, target.State.EquipmentProgression!);
        return Commit(command.RequestId, TransferOperation, Fingerprint(command), owner, 0,
        [Mutation(source with { State = source.State with { EquipmentProgression = step.SourceState } }),
         Mutation(target with { State = target.State with { EquipmentProgression = step.TargetState } })]);
    }

    internal static (ItemRecord, FirstSliceEquipmentDefinition) ReadEquipment(EquipmentItemExpectation expected,
        ItemOwnerRef owner, IReadOnlyDictionary<Guid, ItemRecord> records, DateTimeOffset now)
    {
        ItemRecord item = ReadActive(expected.ItemId, expected.Revision, owner, records, now);
        Require(FirstSliceEquipmentDefinitions.TryResolve(item.DefinitionType, item.DefinitionName,
            item.DefinitionVersion, out FirstSliceEquipmentDefinition definition), "unsupported_equipment_definition");
        Require(item.State.Quantity == 1, "invalid_equipment_quantity");
        Require(item.Location.ContainerType == ItemContainerType.Inventory
            || (item.Location.ContainerType == ItemContainerType.Equipment
                && item.Location.SlotIndex == definition.EquipmentSlotIndex), "invalid_equipment_location");
        Require(FirstSliceEquipmentProgression.IsValidState(item.State.EquipmentProgression), "equipment_not_initialized");
        Require(item.State.EquipmentProgression!.Level == expected.Level, "equipment_level_conflict");
        return (item, definition);
    }

    internal static ItemRecord ReadActive(Guid itemId, long revision, ItemOwnerRef owner,
        IReadOnlyDictionary<Guid, ItemRecord> records, DateTimeOffset now)
    {
        Require(records.TryGetValue(itemId, out ItemRecord? item) && item is not null, "item_not_found");
        ItemRecord record = item!;
        Require(record.ItemId == itemId && ItemRecordRules.TryValidateRecord(record, out _), "invalid_item_record");
        Require(record.Revision == revision, "item_revision_conflict");
        Require(record.Owner == owner && record.Location.ContainerId == owner.OwnerId, "item_owner_mismatch");
        Require(record.LifecycleState == ItemLifecycleState.Active, "item_not_active");
        Require(record.Metadata.BindState == ItemBindState.CharacterBound, "item_not_character_bound");
        Require(!record.Metadata.IsLocked, "item_locked");
        Require(record.Metadata.ExpiresAtUtc is null || record.Metadata.ExpiresAtUtc > now, "item_expired");
        return record;
    }

    private static FirstSliceEquipmentUpgradeCommand Normalize(FirstSliceEquipmentUpgradeCommand command)
    {
        ValidateCommon(command.RequestId, command.CharacterId, command.RuleVersion);
        ValidateExpected(command.Equipment);
        Require(command.OreStacks is not null && command.OreStacks.Count is >= 1 and <= MaximumOreStacks,
            "invalid_ore_stack_count");
        EquipmentOreExpectation[] stacks = command.OreStacks!.ToArray();
        HashSet<Guid> ids = [command.Equipment.ItemId];
        foreach (EquipmentOreExpectation expected in stacks)
        {
            Require(expected is not null, "invalid_ore_expectation");
            ValidateReference(expected!.ItemId, expected.Revision);
            Require(ids.Add(expected.ItemId), "duplicate_item_id");
        }
        return command with { OreStacks = stacks.OrderBy(stack => stack.ItemId.ToString("N"), StringComparer.Ordinal).ToArray() };
    }

    private static void Validate(FirstSliceEquipmentTransferCommand command)
    {
        ValidateCommon(command.RequestId, command.CharacterId, command.RuleVersion);
        ValidateExpected(command.Source);
        ValidateExpected(command.Target);
        Require(command.Source.ItemId != command.Target.ItemId, "duplicate_item_id");
    }

    internal static void ValidateCommon(Guid requestId, Guid characterId, int version)
    {
        Require(requestId != Guid.Empty && characterId != Guid.Empty, "invalid_equipment_command_identity");
        Require(version == FirstSliceEquipmentProgression.RuleVersion, "unsupported_equipment_rule_version");
    }

    internal static void ValidateExpected(EquipmentItemExpectation expected)
    {
        Require(expected is not null, "invalid_equipment_expectation");
        ValidateReference(expected!.ItemId, expected.Revision);
        Require(expected.Level is >= 0 and <= FirstSliceEquipmentProgression.MaximumLevel, "invalid_equipment_level");
    }

    internal static void ValidateReference(Guid id, long revision) => Require(id != Guid.Empty
        && revision is > 0 and < long.MaxValue, "invalid_item_expectation");

    private static ItemOwnerRef Owner(Guid characterId) => new(ItemOwnerType.Character, characterId.ToString("D"));
    private static ItemRecordMutation Mutation(ItemRecord record) =>
        new(record.Revision, ItemRecordRules.SnapshotWithRevision(record, record.Revision));

    private static ItemRepositoryCommitRequest Commit(Guid id, string operation, string fingerprint,
        ItemOwnerRef owner, int affected, IReadOnlyList<ItemRecordMutation> mutations)
    {
        ItemRepositoryCommitRequest result = new(id, operation, fingerprint, owner, affected, mutations);
        Require(ItemRecordRules.TryValidateCommit(result, out _), "invalid_equipment_plan");
        return result;
    }

    private static string Hash<T>(string operation, T command) => Convert.ToHexStringLower(SHA256.HashData(
        Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new { operation, command }))));

    private static void Require(bool condition, string code)
    {
        if (!condition) throw new EquipmentProgressionRuleException(code);
    }
}
