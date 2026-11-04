using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Evidence;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

public sealed class PostgresEvidenceReadStore(NpgsqlDataSource source, OperationsStorageTrace trace) : IEvidenceReadStore
{
    public Task<Result<Page<EvidenceItem>>> ReadAsync(CharacterHeader target, EvidenceSourceKind kind, EvidenceQuery query, CancellationToken ct) =>
        ReadCoreAsync(target, kind, query, null, ct);
    public async Task<Result<EvidenceItem>> FindAsync(CharacterHeader target, EvidenceSourceKind kind, Guid key, CancellationToken ct)
    {
        var result = await ReadCoreAsync(target, kind, new(target.CharacterId, DateTimeOffset.UnixEpoch, DateTimeOffset.UnixEpoch.AddDays(1), 1), key, ct);
        if (!result.IsSuccess) return Result<EvidenceItem>.Fail(result.Error!);
        return result.Value.Items.Count == 0 ? Result<EvidenceItem>.Fail(EvidenceMapping.Missing) : Result<EvidenceItem>.Success(result.Value.Items[0]);
    }
    private async Task<Result<Page<EvidenceItem>>> ReadCoreAsync(CharacterHeader target, EvidenceSourceKind kind, EvidenceQuery query, Guid? key, CancellationToken ct)
    {
        try { return await ReadSqlAsync(target, kind, query, key, ct); }
        catch (Exception error) when (error is JsonException or FormatException or InvalidCastException)
        { return Result<Page<EvidenceItem>>.Fail(new(FailureKind.Unavailable, "ops.evidence.source_unreadable")); }
    }
    private Task<Result<Page<EvidenceItem>>> ReadSqlAsync(CharacterHeader target, EvidenceSourceKind kind, EvidenceQuery query, Guid? key, CancellationToken ct) =>
        PostgresReadSession.RunAsync(source, trace, async session =>
        {
            // Only fixed SQL fragments are composed. IDs, JSON containment and cursor values remain parameters.
            var (table, id, time, columns, scope) = Sql(kind);
            string range = key.HasValue ? $"{id}=@key" : $"{time}>=@from AND {time}<@to AND (@after_id IS NULL OR ({time},{id})<(@after_time,@after_id))";
            await using var command = session.Command($"SELECT {id},{time},{columns} FROM {table} WHERE ({scope}) AND {range} ORDER BY {time} DESC,{id} DESC LIMIT @limit;");
            command.Parameters.AddWithValue("character", target.CharacterId);
            command.Parameters.AddWithValue("character_text", target.CharacterId.ToString("D"));
            command.Parameters.AddWithValue("account", target.AccountId);
            command.Parameters.AddWithValue("character_list", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(new[] { target.CharacterId }));
            command.Parameters.AddWithValue("character_owner", NpgsqlDbType.Jsonb, OwnerJson("Character", target.CharacterId.ToString("D")));
            command.Parameters.AddWithValue("account_owner", NpgsqlDbType.Jsonb, OwnerJson("Account", target.AccountId));
            command.Parameters.AddWithValue("limit", key.HasValue ? 1 : query.Limit + 1);
            if (key.HasValue) command.Parameters.AddWithValue("key", key.Value);
            else
            {
                command.Parameters.AddWithValue("from", query.FromUtc);
                command.Parameters.AddWithValue("to", query.ToUtc);
                command.Parameters.AddWithValue("after_id", NpgsqlDbType.Uuid, (object?)query.After?.For(kind)?.Id ?? DBNull.Value);
                command.Parameters.AddWithValue("after_time", NpgsqlDbType.TimestampTz, (object?)query.After?.For(kind)?.Timestamp ?? DBNull.Value);
            }
            List<EvidenceItem> rows = [];
            await using var reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                Guid sourceKey = reader.GetGuid(0);
                DateTimeOffset occurred = reader.GetFieldValue<DateTimeOffset>(1);
                if (kind == EvidenceSourceKind.Settlement)
                    rows.Add(EvidenceMapping.Settlement(sourceKey, target.CharacterId, occurred, reader.GetString(2)));
                else if (kind == EvidenceSourceKind.CurrencyReceipt)
                {
                    var changes = JsonSerializer.Deserialize<CurrencyChangeResult[]>(reader.GetString(3), EvidenceMapping.StoredJson) ?? [];
                    rows.Add(EvidenceMapping.Currency(new(CurrencyTransactionStatus.Committed, sourceKey, target.CharacterId,
                        reader.GetString(2), "", "", changes, occurred)));
                }
                else
                {
                    bool actor = reader.GetBoolean(5);
                    Guid[] ids = JsonSerializer.Deserialize<Guid[]>(reader.GetString(6)) ?? [];
                    rows.Add(new(kind, sourceKey, occurred, target.CharacterId, actor ? reader.GetString(3) : "CharacterOrAccount",
                        actor ? reader.GetString(4) : target.CharacterId.ToString("D"), null, sourceKey,
                        Array.AsReadOnly(ids.Take(64).ToArray()), EvidenceStatus.Delivered,
                        actor && reader.GetString(3) == "Character" ? EvidenceLinkReason.DirectReceipt : EvidenceLinkReason.OwnerScope,
                        reader.GetString(2), [], DetailsTruncated: ids.Length > 64));
                }
            }
            return Result<Page<EvidenceItem>>.Success(ReadMapping.Page(rows, query.Limit, i => new(i.SourceKey, i.OccurredAtUtc)));
        }, ct);

    private static string OwnerJson(string type, string id) => JsonSerializer.Serialize(new[] { new { owner = new { type, ownerId = id } } });
    private const string ActorMatch = "((r.actor_type='Character' AND r.actor_id=@character_text) OR (r.actor_type='Account' AND r.actor_id=@account))";
    private static (string Table, string Id, string Time, string Columns, string Scope) Sql(EvidenceSourceKind kind) => kind switch
    {
        EvidenceSourceKind.CurrencyReceipt => ("currency_transaction_receipts r", "r.request_id", "r.committed_at", """
            r.operation, COALESCE((SELECT jsonb_agg(value) FROM
                (SELECT value FROM jsonb_array_elements(r.result_changes) LIMIT 17) bounded),'[]'::jsonb)::text
            """, "r.character_id=@character"),
        EvidenceSourceKind.ItemReceipt => ("item_transaction_receipts r", "r.request_id", "r.committed_at",
            $"r.operation,r.actor_type,r.actor_id,{ActorMatch}," + """
            COALESCE((SELECT jsonb_agg(item_id) FROM (SELECT DISTINCT value->'state'->>'instanceId' item_id
                FROM jsonb_array_elements(r.result_records) WHERE
            """ + ActorMatch + """
                OR (value->'owner'->>'type'='Character' AND value->'owner'->>'ownerId'=@character_text)
                OR (value->'owner'->>'type'='Account' AND value->'owner'->>'ownerId'=@account)
                ORDER BY item_id LIMIT 65) bounded),'[]'::jsonb)::text
            """, ActorMatch + " OR r.result_records @> @character_owner OR r.result_records @> @account_owner"),
        _ => ("dungeon_reward_settlements r", "r.dungeon_session_id", "r.updated_at", "r.state", "r.character_ids @> @character_list")
    };
}
