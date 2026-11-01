using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : IEquipmentProgressionRepository
{
    public Task<ItemRepositoryCommitResult> CraftEquipmentAsync(string steamId, FirstSliceEquipmentCraftCommand command,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        command = command with { TokenStacks = command.TokenStacks?.ToArray()! };
        string fingerprint = FirstSliceEquipmentCraftingPlanner.Fingerprint(command);
        return CommitEquipmentAsync(steamId, command.CharacterId, command.RequestId,
            FirstSliceEquipmentCraftingPlanner.CraftOperation, fingerprint, command.TokenStacks.Select(stack => stack.ItemId).ToArray(),
            records => FirstSliceEquipmentCraftingPlanner.PlanCraft(command, records, Guid.NewGuid(), now), now, cancellationToken, includeInventory: true);
    }

    public Task<ItemRepositoryCommitResult> DismantleEquipmentAsync(string steamId, FirstSliceEquipmentDismantleCommand command,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        string fingerprint = FirstSliceEquipmentCraftingPlanner.Fingerprint(command);
        return CommitEquipmentAsync(steamId, command.CharacterId, command.RequestId,
            FirstSliceEquipmentCraftingPlanner.DismantleOperation, fingerprint, [command.Equipment.ItemId],
            records => FirstSliceEquipmentCraftingPlanner.PlanDismantle(command, records, Guid.NewGuid(), now), now, cancellationToken, includeInventory: true);
    }

    public Task<ItemRepositoryCommitResult> UpgradeEquipmentAsync(string steamId, FirstSliceEquipmentUpgradeCommand command,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        command = command with { OreStacks = command.OreStacks?.ToArray()! };
        string fingerprint = FirstSliceEquipmentCommandPlanner.Fingerprint(command);
        return CommitEquipmentAsync(steamId, command.CharacterId, command.RequestId,
            FirstSliceEquipmentCommandPlanner.UpgradeOperation, fingerprint,
            [command.Equipment.ItemId, .. command.OreStacks.Select(stack => stack.ItemId)],
            records => FirstSliceEquipmentCommandPlanner.PlanUpgrade(command, records, now), now, cancellationToken);
    }

    public Task<ItemRepositoryCommitResult> TransferEquipmentAsync(string steamId, FirstSliceEquipmentTransferCommand command,
        DateTimeOffset now, CancellationToken cancellationToken) => CommitEquipmentAsync(steamId,
            command.CharacterId, command.RequestId, FirstSliceEquipmentCommandPlanner.TransferOperation,
            FirstSliceEquipmentCommandPlanner.Fingerprint(command), [command.Source.ItemId, command.Target.ItemId],
            records => FirstSliceEquipmentCommandPlanner.PlanTransfer(command, records, now), now, cancellationToken);

    private Task<ItemRepositoryCommitResult> CommitEquipmentAsync(string steamId, Guid characterId, Guid requestId,
        string operation, string fingerprint, IReadOnlyList<Guid> itemIds,
        Func<IReadOnlyDictionary<Guid, ItemRecord>, ItemRepositoryCommitRequest> plan,
        DateTimeOffset now, CancellationToken cancellationToken, bool includeInventory = false)
    {
        cancellationToken.ThrowIfCancellationRequested();
        InMemoryItemRepository items = _starterItems
            ?? throw new InvalidOperationException("Equipment commands require the shared character/item repository.");
        // Pickup already uses dungeon -> item; creation uses item -> character.
        // No character -> item inversion, no await while holding a Monitor.
        lock (_dungeonGate)
        lock (items.TransactionSyncRoot)
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!_characters.TryGetValue(characterId, out GameCharacter? character) || character.SteamId != steamId)
                throw new EquipmentProgressionRuleException("character_not_owned");
            ItemOwnerRef owner = new(ItemOwnerType.Character, characterId.ToString("D"));
            ItemRepositoryCommitResult? receipt = items.TryGetCommitResultAsync(requestId, cancellationToken).GetAwaiter().GetResult();
            if (receipt is not null) return Task.FromResult(EquipmentProgressionReceipt.Replay(
                receipt, requestId, operation, fingerprint, owner));
            if (_characterLeases.TryGetValue(characterId, out CharacterLeaseRecord? lease) && lease.ExpiresAt > now)
                throw new EquipmentProgressionRuleException("active_dungeon_equipment_locked");
            Dictionary<Guid, ItemRecord> records = [];
            foreach (Guid id in itemIds)
            {
                ItemRecord? item = items.FindAsync(id, cancellationToken).GetAwaiter().GetResult();
                if (item is not null && item.Owner == owner) records.Add(id, item);
            }
            if (includeInventory)
                foreach (ItemRecord item in items.FindByOwnerAsync(owner, false, int.MaxValue, cancellationToken).GetAwaiter().GetResult())
                    if (DungeonRewardDeliveryPlanner.IsInventoryRecord(item, owner)) records.TryAdd(item.ItemId, item);
            ItemRepositoryCommitRequest request = plan(records);
            ItemRepositoryCommitResult result = items.CommitAsync(request, now, cancellationToken).GetAwaiter().GetResult();
            return Task.FromResult(EquipmentProgressionReceipt.Copy(result));
        }
    }
}
