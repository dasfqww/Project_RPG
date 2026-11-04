using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Inventory;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

public sealed class PostgresInventoryReadStore(NpgsqlDataSource source, OperationsStorageTrace trace) : IInventoryReadStore
{
    public Task<Result<Page<ItemSummary>>> ReadAsync(Guid characterId, InventoryContainer container, PageRequest page, CancellationToken ct) =>
        PostgresReadSession.RunAsync(source, trace, async session =>
        {
            if (!await session.CharacterExistsAsync(characterId, ct)) return Result<Page<ItemSummary>>.Fail(Failures.CharacterMissing);
            await using NpgsqlCommand command = session.Command("""
                SELECT item_id,definition_type,definition_name,definition_version,quantity,revision,
                    container_type,container_id,slot_index,lifecycle_state,bind_state,is_locked,expires_at,
                    durability_current,durability_maximum
                FROM item_records WHERE owner_type='Character' AND owner_id=@owner AND container_type=@container
                    AND (@after IS NULL OR item_id>@after) ORDER BY item_id LIMIT @limit;
                """);
            command.Parameters.AddWithValue("owner", characterId.ToString("D"));
            command.Parameters.AddWithValue("container", container.ToString());
            command.Parameters.AddWithValue("after", NpgsqlDbType.Uuid, (object?)page.After?.Id ?? DBNull.Value);
            command.Parameters.AddWithValue("limit", page.Limit + 1);
            List<ItemSummary> rows = [];
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
                rows.Add(new(reader.GetGuid(0), reader.GetString(1), reader.GetString(2), reader.GetInt32(3),
                    reader.GetInt32(4), reader.GetInt64(5), reader.GetString(6), reader.GetString(7), reader.GetInt32(8),
                    reader.GetString(9), reader.GetString(10), reader.GetBoolean(11),
                    reader.IsDBNull(12) ? null : reader.GetFieldValue<DateTimeOffset>(12), new(reader.GetInt32(13), reader.GetInt32(14))));
            return Result<Page<ItemSummary>>.Success(ReadMapping.Page(rows, page.Limit, i => new(i.ItemId)));
        }, ct);
}
