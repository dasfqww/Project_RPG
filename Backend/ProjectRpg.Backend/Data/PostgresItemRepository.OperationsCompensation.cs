using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresItemRepository
{
    internal static async Task<ItemRepositoryCommitResult?> LockOperationsReceiptAsync(NpgsqlConnection connection,
        NpgsqlTransaction transaction, Guid characterId, Guid requestId, CancellationToken ct)
    {
        await AcquireOwnerLocksAsync(connection, transaction, [new(ItemOwnerType.Character, characterId.ToString("D"))], ct);
        await AcquireRequestLockAsync(connection, transaction, requestId, ct);
        return await ReadReceiptAsync(connection, transaction, requestId, ct);
    }
    internal static Task<ItemRepositoryCommitResult?> ReadOperationsReceiptAsync(NpgsqlConnection connection,
        NpgsqlTransaction transaction, Guid requestId, CancellationToken ct) => ReadReceiptAsync(connection, transaction, requestId, ct);
}
