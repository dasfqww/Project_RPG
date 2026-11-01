using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain;

public sealed record EquipmentTokenExpectation(Guid ItemId, long Revision);
public sealed record FirstSliceEquipmentCraftCommand(Guid RequestId, Guid CharacterId, int RuleVersion,
    string TargetDefinitionName, int TargetDefinitionVersion, IReadOnlyList<EquipmentTokenExpectation> TokenStacks);
public sealed record FirstSliceEquipmentDismantleCommand(Guid RequestId, Guid CharacterId, int RuleVersion,
    EquipmentItemExpectation Equipment);

/// <summary>Server-read, transaction-protected inputs only; receipt recovery precedes planning.</summary>
public static class FirstSliceEquipmentCraftingPlanner
{
    public const string CraftOperation = "FirstSliceEquipmentCraft";
    public const string DismantleOperation = "FirstSliceEquipmentDismantle";
    public const int TokenCost = 8;
    public const int DismantleTokenReturn = 1;
    public const int MaximumTokenStacks = ItemRecordRules.MaximumMutationCount - 1;

    public static string Fingerprint(FirstSliceEquipmentCraftCommand command) => Hash(CraftOperation, Normalize(command));
    public static string Fingerprint(FirstSliceEquipmentDismantleCommand command)
    {
        Validate(command);
        return Hash(DismantleOperation, command);
    }

    public static ItemRepositoryCommitRequest PlanCraft(FirstSliceEquipmentCraftCommand command,
        IReadOnlyDictionary<Guid, ItemRecord> records, Guid resultItemId, DateTimeOffset now)
    {
        command = Normalize(command);
        Require(FirstSliceEquipmentDefinitions.TryResolve(FirstSliceEquipmentDefinitions.DefinitionType,
            command.TargetDefinitionName, command.TargetDefinitionVersion, out var definition)
            && IsTarget(definition.Model), "unsupported_craft_target");
        ItemOwnerRef owner = new(ItemOwnerType.Character, command.CharacterId.ToString("D"));
        List<ItemRecord> stacks = [];
        foreach (var expected in command.TokenStacks)
        {
            ItemRecord token = FirstSliceEquipmentCommandPlanner.ReadActive(expected.ItemId, expected.Revision, owner, records, now);
            Require(token.DefinitionType == FirstSliceEquipmentDefinitions.DefinitionType
                && token.DefinitionName == FirstSliceEquipmentDefinitions.TokenDefinitionName
                && token.DefinitionVersion == FirstSliceEquipmentDefinitions.DefinitionVersion, "unsupported_token_definition");
            Require(DungeonRewardDeliveryPlanner.IsInventoryRecord(token, owner)
                && token.State.Quantity <= FirstSliceEquipmentDefinitions.TokenMaximumStackSize
                && token.State.EquipmentProgression is null, "invalid_token_stack");
            stacks.Add(token);
        }
        Require(stacks.Sum(item => (long)item.State.Quantity) >= TokenCost, "insufficient_tokens");
        List<ItemRecordMutation> costs = [];
        int remaining = TokenCost;
        foreach (ItemRecord stack in stacks)
        {
            if (remaining == 0) break;
            int used = Math.Min(remaining, stack.State.Quantity);
            ItemRecord next = stack with { State = stack.State with { Quantity = stack.State.Quantity - used } };
            costs.Add(Mutation(next.State.Quantity == 0 ? Consume(next) : next));
            remaining -= used;
        }
        ItemRecord reward = Reward(command.RequestId, resultItemId, owner, command.TargetDefinitionName,
            CraftOperation, FirstSliceEquipmentProgression.Create());
        return Deliver(command.RequestId, CraftOperation, Fingerprint(command), owner, TokenCost,
            records, costs, reward, 1);
    }

    public static ItemRepositoryCommitRequest PlanDismantle(FirstSliceEquipmentDismantleCommand command,
        IReadOnlyDictionary<Guid, ItemRecord> records, Guid resultItemId, DateTimeOffset now)
    {
        Validate(command);
        ItemOwnerRef owner = new(ItemOwnerType.Character, command.CharacterId.ToString("D"));
        (ItemRecord item, FirstSliceEquipmentDefinition definition) =
            FirstSliceEquipmentCommandPlanner.ReadEquipment(command.Equipment, owner, records, now);
        Require(IsTarget(definition.Model), "starter_equipment_not_dismantlable");
        Require(item.State.EquipmentProgression!.Level == 0, "enhanced_equipment_not_dismantlable");
        Require(item.Location.ContainerType == ItemContainerType.Equipment
            || DungeonRewardDeliveryPlanner.IsInventoryRecord(item, owner), "invalid_equipment_location");
        ItemRecord reward = Reward(command.RequestId, resultItemId, owner,
            FirstSliceEquipmentDefinitions.TokenDefinitionName, DismantleOperation, null);
        return Deliver(command.RequestId, DismantleOperation, Fingerprint(command), owner, DismantleTokenReturn,
            records, [Mutation(Consume(item))], reward, FirstSliceEquipmentDefinitions.TokenMaximumStackSize);
    }

    private static bool IsTarget(FirstSliceEquipmentModel model) => model is FirstSliceEquipmentModel.EnduranceGreatSword
        or FirstSliceEquipmentModel.CirculationGreatSword or FirstSliceEquipmentModel.TargetChest or FirstSliceEquipmentModel.TargetGloves;

    private static ItemRecord Consume(ItemRecord item) => item with
    {
        State = item.State with { Quantity = 0 },
        LifecycleState = ItemLifecycleState.Consumed,
        Location = new(ItemContainerType.Terminal, string.Empty, -1)
    };

    private static ItemRecord Reward(Guid requestId, Guid itemId, ItemOwnerRef owner, string name,
        string operation, FirstSliceEquipmentProgressionState? growth) => new(
        FirstSliceEquipmentDefinitions.DefinitionType, name, FirstSliceEquipmentDefinitions.DefinitionVersion, owner,
        new(ItemContainerType.Mail, $"{operation}.{requestId:N}", 0),
        new(itemId, 0, 1, [], [], growth), 0, ItemLifecycleState.Active,
        new(ItemBindState.CharacterBound, new(0, 0), null, operation, false));

    private static ItemRepositoryCommitRequest Deliver(Guid requestId, string operation, string fingerprint,
        ItemOwnerRef owner, int affected, IReadOnlyDictionary<Guid, ItemRecord> records,
        IReadOnlyList<ItemRecordMutation> costs, ItemRecord reward, int maximumStack)
    {
        Require(reward.ItemId != Guid.Empty && !records.ContainsKey(reward.ItemId), "invalid_result_item_id");
        Dictionary<Guid, ItemRecord> working = new(records);
        foreach (var cost in costs) working[cost.NewRecord.ItemId] = cost.NewRecord;
        ItemRepositoryCommitRequest grant = new(requestId, operation, fingerprint, owner, affected, [new(0, reward)]);
        ItemRepositoryCommitRequest delivery = DungeonRewardDeliveryPlanner.PlanEquipmentDelivery(grant, [maximumStack], working.Values.ToArray());
        ItemRepositoryCommitRequest result = grant with { Mutations = [.. costs, .. delivery.Mutations] };
        Require(ItemRecordRules.TryValidateCommit(result, out _), "invalid_equipment_plan");
        return result;
    }

    private static ItemRecordMutation Mutation(ItemRecord item) =>
        new(item.Revision, ItemRecordRules.SnapshotWithRevision(item, item.Revision));

    private static FirstSliceEquipmentCraftCommand Normalize(FirstSliceEquipmentCraftCommand command)
    {
        FirstSliceEquipmentCommandPlanner.ValidateCommon(command.RequestId, command.CharacterId, command.RuleVersion);
        Require(!string.IsNullOrWhiteSpace(command.TargetDefinitionName) && command.TargetDefinitionName.Length <= 256
            && !command.TargetDefinitionName.Any(char.IsControl) && command.TargetDefinitionVersion > 0, "invalid_craft_target");
        Require(command.TokenStacks is not null && command.TokenStacks.Count is >= 1 and <= MaximumTokenStacks, "invalid_token_stack_count");
        var stacks = command.TokenStacks!.ToArray();
        HashSet<Guid> ids = [];
        foreach (var expected in stacks)
        {
            Require(expected is not null, "invalid_token_expectation");
            FirstSliceEquipmentCommandPlanner.ValidateReference(expected!.ItemId, expected.Revision);
            Require(ids.Add(expected.ItemId), "duplicate_item_id");
        }
        return command with { TokenStacks = stacks.OrderBy(item => item.ItemId.ToString("N"), StringComparer.Ordinal).ToArray() };
    }

    private static void Validate(FirstSliceEquipmentDismantleCommand command)
    {
        FirstSliceEquipmentCommandPlanner.ValidateCommon(command.RequestId, command.CharacterId, command.RuleVersion);
        FirstSliceEquipmentCommandPlanner.ValidateExpected(command.Equipment);
    }

    private static string Hash<T>(string operation, T command) => Convert.ToHexStringLower(SHA256.HashData(
        Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new { operation, command }))));
    private static void Require(bool condition, string code)
    {
        if (!condition) throw new EquipmentProgressionRuleException(code);
    }
}
