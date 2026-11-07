namespace ProjectRpg.Backend.Domain;

/// <summary>Server-owned canonical item namespaces and persistent tab/slot encoding.</summary>
public static class InventoryBagLayout
{
    public const int SlotsPerTab = 80;
    public const int TotalSlots = 3 * SlotsPerTab;
    public const string SortOperation = "Item.Sort";
    public static int Tab(string definitionName) =>
        definitionName.StartsWith("GameItem.Equipment.", StringComparison.Ordinal) ? 0 :
        definitionName.StartsWith("GameItem.Consume.", StringComparison.Ordinal) ? 1 : 2;
    public static IEnumerable<int> Slots(ItemRecord item) => Enumerable.Range(Tab(item.DefinitionName) * SlotsPerTab, SlotsPerTab);
    public static bool CanStore(ItemRecord item) => item.Location.SlotIndex >= 0
        && item.Location.SlotIndex < TotalSlots && item.Location.SlotIndex / SlotsPerTab == Tab(item.DefinitionName);
    public static int CommitLimit(string? operation) => operation == SortOperation ? SlotsPerTab : ItemRecordRules.MaximumMutationCount;
    public static bool ValidNewDestinations(ItemRepositoryCommitRequest request) =>
        request.Operation is not ("Item.Move" or "Item.Swap" or "Item.Split" or "Item.ClaimMail" or "Item.Unequip" or "Item.Pickup" or SortOperation)
        || request.Mutations.All(m => m.NewRecord.Location.ContainerType != ItemContainerType.Inventory || CanStore(m.NewRecord));

    // Sorting may only relocate existing items within one tab. Never create, merge,
    // consume, unlock, rebind, alter stats or transfer ownership under this operation.
    public static bool IsSortMutation(ItemRecord? before, ItemRecord after) => before is not null
        && before.LifecycleState == ItemLifecycleState.Active && after.LifecycleState == before.LifecycleState
        && before.Owner == after.Owner && before.Owner.Type == ItemOwnerType.Character
        && before.Location.ContainerType == ItemContainerType.Inventory
        && after.Location.ContainerType == ItemContainerType.Inventory
        && before.Location.ContainerId == before.Owner.OwnerId && after.Location.ContainerId == before.Owner.OwnerId
        && CanStore(before) && CanStore(after) && before.DefinitionType == after.DefinitionType
        && before.DefinitionName == after.DefinitionName && before.DefinitionVersion == after.DefinitionVersion
        && before.Revision == after.Revision && before.Metadata == after.Metadata
        && (!before.Metadata.IsLocked || before.Location == after.Location)
        && before.State.InstanceId == after.State.InstanceId && before.State.GenerationSeed == after.State.GenerationSeed
        && before.State.Quantity == after.State.Quantity && before.State.EquipmentProgression == after.State.EquipmentProgression
        && before.State.InstanceTags.SequenceEqual(after.State.InstanceTags, StringComparer.Ordinal)
        && before.State.StatValues.SequenceEqual(after.State.StatValues);
}
