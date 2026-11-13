using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresItemRepository
{
    // The caller holds account/character/lease, Riding request and roster locks, in that order.
    internal static async Task<ItemRecord?> LockRidingItemAsync(NpgsqlConnection connection, NpgsqlTransaction tx,
        Guid characterId, Guid itemId, Guid childRequestId, CancellationToken ct)
    {
        var owner = new ItemOwnerRef(ItemOwnerType.Character, characterId.ToString("D"));
        await AcquireOwnerLocksAsync(connection, tx, [owner], ct);
        await AcquireRequestLockAsync(connection, tx, childRequestId, ct);
        await using var query = new NpgsqlCommand($"""
            SELECT {ItemColumns} FROM item_records WHERE item_id=@id AND owner_type='Character' AND owner_id=@owner FOR UPDATE;
            """, connection, tx);
        query.Parameters.AddWithValue("id", itemId); query.Parameters.AddWithValue("owner", owner.OwnerId);
        await using var reader = await query.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct) ? ReadItemRecord(reader) : null;
    }

    internal static Task<ItemRepositoryCommitResult?> ReadRidingChildAsync(NpgsqlConnection connection, NpgsqlTransaction tx,
        Guid requestId, CancellationToken ct) => ReadReceiptAsync(connection, tx, requestId, ct);
}
