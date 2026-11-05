namespace ProjectRpg.Backend.Domain;

/// <summary>Plans inventory-first delivery from a transaction-protected inventory snapshot.</summary>
public static class DungeonRewardDeliveryPlanner
{
    // Keep aligned with RPGItemInventoryPolicy::SlotCapacity. Not supplied by clients.
    public const int InventorySlotCapacity = 80;
    public const int MaximumMutationCount = InventorySlotCapacity + DungeonRewardRules.MaximumItemRewardCount;

    public static ItemRepositoryCommitRequest Plan(
        ItemRepositoryCommitRequest grant,
        IReadOnlyList<int>? maxStackSizes,
        IReadOnlyList<ItemRecord> inventory,
        IReadOnlySet<Guid>? reservedItemIds = null)
        => PlanDelivery(grant, maxStackSizes, inventory, requireDungeonReward: true, reservedItemIds);

    internal static ItemRepositoryCommitRequest PlanEquipmentDelivery(ItemRepositoryCommitRequest grant,
        IReadOnlyList<int> maxStackSizes, IReadOnlyList<ItemRecord> inventory)
        => PlanDelivery(grant, maxStackSizes, inventory, requireDungeonReward: false);

    private static ItemRepositoryCommitRequest PlanDelivery(ItemRepositoryCommitRequest grant,
        IReadOnlyList<int>? maxStackSizes, IReadOnlyList<ItemRecord> inventory, bool requireDungeonReward,
        IReadOnlySet<Guid>? reservedItemIds = null)
    {
        if (!ItemRecordRules.TryValidateCommit(grant, out string error)
            || (requireDungeonReward && grant.Operation != "DungeonReward")
            || grant.Actor.Type != ItemOwnerType.Character
            || (maxStackSizes is not null && maxStackSizes.Count != grant.Mutations.Count))
        {
            throw new ArgumentException($"Invalid dungeon item grant: {error}");
        }

        Dictionary<Guid, ItemRecord> working = inventory
            .Where(record => IsInventoryRecord(record, grant.Actor))
            .ToDictionary(record => record.ItemId);
        HashSet<int> occupied = working.Values.Select(record => record.Location.SlotIndex).ToHashSet();
        Dictionary<Guid, ItemRecordMutation> mutations = [];

        for (int index = 0; index < grant.Mutations.Count; index++)
        {
            ItemRecordMutation mutation = grant.Mutations[index];
            ItemRecord reward = mutation.NewRecord;
            int maxStackSize = maxStackSizes?[index] ?? 0;
            if (mutation.ExpectedRevision != 0 || reward.Owner != grant.Actor
                || reward.LifecycleState != ItemLifecycleState.Active
                || reward.Location.ContainerType != ItemContainerType.Mail
                || maxStackSize < 0 || (maxStackSize > 0 && reward.State.Quantity > maxStackSize))
            {
                throw new ArgumentException("Invalid dungeon item delivery limit or source record.");
            }

            // Old persisted settlements lack definition stack limits: preserve their mail-only contract.
            if (maxStackSize == 0)
            {
                mutations.Add(reward.ItemId, mutation);
                continue;
            }

            int remaining = reward.State.Quantity;
            foreach (ItemRecord stack in working.Values
                .OrderBy(record => record.Location.SlotIndex).ThenBy(record => record.ItemId).ToArray())
            {
                if (remaining == 0) break;
                if (reservedItemIds?.Contains(stack.ItemId) == true || !CanMerge(stack, reward) || stack.State.Quantity >= maxStackSize) continue;
                int transferred = Math.Min(remaining, maxStackSize - stack.State.Quantity);
                ItemRecord updated = stack with
                {
                    State = stack.State with { Quantity = stack.State.Quantity + transferred }
                };
                working[stack.ItemId] = updated;
                mutations[stack.ItemId] = new ItemRecordMutation(stack.Revision, updated);
                remaining -= transferred;
            }

            if (remaining == 0) continue;
            int freeSlot = Enumerable.Range(0, InventorySlotCapacity)
                .FirstOrDefault(slot => !occupied.Contains(slot), -1);
            ItemRecord delivered = reward with
            {
                State = reward.State with { Quantity = remaining },
                Location = freeSlot >= 0
                    ? new ItemLocation(ItemContainerType.Inventory, grant.Actor.OwnerId, freeSlot)
                    : reward.Location
            };
            mutations.Add(delivered.ItemId, new ItemRecordMutation(0, delivered));
            if (freeSlot >= 0)
            {
                occupied.Add(freeSlot);
                working.Add(delivered.ItemId, delivered);
            }
        }

        return grant with { Mutations = mutations.Values.ToArray() };
    }

    internal static bool IsInventoryRecord(ItemRecord record, ItemOwnerRef owner)
        => record.Owner == owner && record.LifecycleState == ItemLifecycleState.Active
            && record.Location.ContainerType == ItemContainerType.Inventory
            && record.Location.ContainerId == owner.OwnerId
            && record.Location.SlotIndex is >= 0 and < InventorySlotCapacity;

    private static bool CanMerge(ItemRecord stack, ItemRecord reward)
        => stack.State.EquipmentProgression is null && reward.State.EquipmentProgression is null
            && stack.DefinitionType == reward.DefinitionType
            && stack.DefinitionName == reward.DefinitionName
            && stack.DefinitionVersion == reward.DefinitionVersion
            && !stack.Metadata.IsLocked && !reward.Metadata.IsLocked
            && stack.Metadata.BindState == reward.Metadata.BindState
            && stack.Metadata.Durability == reward.Metadata.Durability
            && stack.Metadata.ExpiresAtUtc == reward.Metadata.ExpiresAtUtc
            && stack.State.InstanceTags.Order(StringComparer.Ordinal)
                .SequenceEqual(reward.State.InstanceTags.Order(StringComparer.Ordinal))
            && stack.State.StatValues.OrderBy(value => value.StatTag, StringComparer.Ordinal)
                .SequenceEqual(reward.State.StatValues.OrderBy(value => value.StatTag, StringComparer.Ordinal));
}
