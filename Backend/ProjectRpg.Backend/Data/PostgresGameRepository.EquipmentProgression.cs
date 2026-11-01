using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : IEquipmentProgressionRepository
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

    private async Task<ItemRepositoryCommitResult> CommitEquipmentAsync(string steamId, Guid characterId, Guid requestId,
        string operation, string fingerprint, IReadOnlyList<Guid> itemIds,
        Func<IReadOnlyDictionary<Guid, ItemRecord>, ItemRepositoryCommitRequest> plan,
        DateTimeOffset now, CancellationToken cancellationToken, bool includeInventory = false)
    {
        PostgresItemRepository items = equipmentItems
            ?? throw new InvalidOperationException("Equipment commands require an injected item repository.");
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        // Admission inserts FK references to this character. FOR UPDATE conflicts
        // with FK KEY SHARE, closing the absent-lease check/insert race.
        await using (NpgsqlCommand owner = new("""
            SELECT character_id FROM characters
            WHERE character_id = @id AND steam_id = @steam FOR UPDATE;
            """, connection, transaction))
        {
            owner.Parameters.AddWithValue("id", characterId);
            owner.Parameters.AddWithValue("steam", steamId);
            if (await owner.ExecuteScalarAsync(cancellationToken) is not Guid)
                throw new EquipmentProgressionRuleException("character_not_owned");
        }
        ItemRepositoryCommitResult result = await items.CommitEquipmentInTransactionAsync(connection, transaction,
            characterId, requestId, operation, fingerprint, itemIds, plan, async () =>
            {
                // Lock even an expired row against heartbeat renewal. Admission
                // cleanup can hold that row BEFORE requesting the character FK;
                // never wait here while holding the character lock (lock inversion).
                await using NpgsqlCommand lease = new("""
                    SELECT expires_at FROM character_session_leases WHERE character_id = @id FOR UPDATE NOWAIT;
                    """, connection, transaction);
                lease.Parameters.AddWithValue("id", characterId);
                try
                {
                    if (await lease.ExecuteScalarAsync(cancellationToken) is DateTime expiry && expiry > now.UtcDateTime)
                        throw new EquipmentProgressionRuleException("active_dungeon_equipment_locked");
                }
                catch (PostgresException error) when (error.SqlState == PostgresErrorCodes.LockNotAvailable)
                {
                    throw new EquipmentProgressionRuleException("equipment_lease_busy");
                }
            }, now, cancellationToken, includeInventory);
        if (result.Status is ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted)
            await transaction.CommitAsync(cancellationToken);
        else await transaction.RollbackAsync(cancellationToken);
        return result;
    }
}
