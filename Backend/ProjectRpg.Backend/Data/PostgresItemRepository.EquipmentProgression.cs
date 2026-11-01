using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresItemRepository
{
    internal async Task<ItemRepositoryCommitResult> CommitEquipmentInTransactionAsync(
        NpgsqlConnection connection, NpgsqlTransaction transaction, Guid characterId, Guid requestId,
        string operation, string fingerprint, IReadOnlyList<Guid> itemIds,
        Func<IReadOnlyDictionary<Guid, ItemRecord>, ItemRepositoryCommitRequest> plan,
        Func<Task> ensureNoLease, DateTimeOffset now, CancellationToken cancellationToken, bool includeInventory = false)
    {
        ItemOwnerRef owner = new(ItemOwnerType.Character, characterId.ToString("D"));
        await AcquireOwnerLocksAsync(connection, transaction, [owner], cancellationToken);
        await AcquireRequestLockAsync(connection, transaction, requestId, cancellationToken);
        ItemRepositoryCommitResult? receipt = await ReadReceiptAsync(connection, transaction, requestId, cancellationToken);
        if (receipt is not null) return EquipmentProgressionReceipt.Replay(receipt, requestId, operation, fingerprint, owner);
        await ensureNoLease();
        Dictionary<Guid, ItemRecord> records = [];
        foreach (Guid id in itemIds.Order())
        {
            // Never lock another character's item on an untrusted requested ID.
            await using NpgsqlCommand query = new($"""
                SELECT {ItemColumns} FROM item_records WHERE item_id = @id
                  AND owner_type = 'Character' AND owner_id = @owner FOR UPDATE;
                """, connection, transaction);
            query.Parameters.AddWithValue("id", id);
            query.Parameters.AddWithValue("owner", owner.OwnerId);
            await using NpgsqlDataReader reader = await query.ExecuteReaderAsync(cancellationToken);
            if (await reader.ReadAsync(cancellationToken)) records.Add(id, ReadItemRecord(reader));
        }
        if (includeInventory)
        {
            await using NpgsqlCommand query = new($"""
                SELECT {ItemColumns} FROM item_records WHERE owner_type = 'Character' AND owner_id = @owner
                  AND lifecycle_state = 'Active' AND container_type = 'Inventory' AND container_id = @owner
                  AND slot_index >= 0 AND slot_index < @capacity ORDER BY item_id FOR UPDATE;
                """, connection, transaction);
            query.Parameters.AddWithValue("owner", owner.OwnerId);
            query.Parameters.AddWithValue("capacity", DungeonRewardDeliveryPlanner.InventorySlotCapacity);
            await using NpgsqlDataReader reader = await query.ExecuteReaderAsync(cancellationToken);
            while (await reader.ReadAsync(cancellationToken))
            {
                ItemRecord item = ReadItemRecord(reader);
                records.TryAdd(item.ItemId, item);
            }
        }
        // Reentrant owner/request locks; all records are owned and already locked.
        // Mutations, revision snapshots and success receipt use this SAME transaction.
        return await CommitInTransactionAsync(connection, transaction, plan(records), now, cancellationToken);
    }
}
