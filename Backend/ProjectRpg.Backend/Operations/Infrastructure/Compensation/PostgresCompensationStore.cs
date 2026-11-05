using System.Data;
using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Infrastructure.Reads;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Compensation;

namespace ProjectRpg.Backend.Operations.Infrastructure.Compensation;

public sealed class PostgresCompensationStore(NpgsqlDataSource source, PostgresItemRepository items,
    ICompensationPolicy policy, TimeProvider clock) : ICompensationCommitter, IOperationActionReader
{
    private sealed record Stored(string Hash, OperationAction Receipt);
    private const string Columns = "request_id, operation, committed_at, operator_id, target_scope, target_id, case_id, reason, result_json::text, origin_http_request_id, origin_trace_id, input_hash";
    private static Stored Read(NpgsqlDataReader reader) => new(Convert.ToHexString(reader.GetFieldValue<byte[]>(11)),
        new(reader.GetGuid(0), reader.GetString(1), reader.GetFieldValue<DateTimeOffset>(2), reader.GetString(3),
            new(reader.GetString(4), reader.GetGuid(5)), reader.GetGuid(6), reader.GetString(7),
            JsonSerializer.Deserialize<CompensationEffect>(reader.GetString(8), OperationsHttp.Json) ?? throw new JsonException(),
            reader.GetGuid(9), reader.IsDBNull(10) ? null : reader.GetString(10)));
    private static async Task<Stored?> FindStored(NpgsqlConnection connection, NpgsqlTransaction transaction, Guid id, CancellationToken ct)
    {
        await using NpgsqlCommand query = new($"SELECT {Columns} FROM ops_actions WHERE request_id=@id AND target_scope='Character'", connection, transaction);
        query.Parameters.AddWithValue("id", id);
        await using var reader = await query.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct) ? Read(reader) : null;
    }
    private static async Task<bool> Consistent(NpgsqlConnection connection, NpgsqlTransaction transaction, Stored stored, CancellationToken ct) =>
        CompensationCatalog.Consistent(stored.Receipt, stored.Hash,
            await PostgresItemRepository.ReadOperationsReceiptAsync(connection, transaction, stored.Receipt.RequestId, ct));

    public async Task<Result<ActionCommit>> CommitAsync(CompensationCommand command, CancellationToken ct)
    {
        bool committing = false;
        try
        {
            await using var connection = await source.OpenConnectionAsync(ct);
            await using var transaction = await connection.BeginTransactionAsync(ct);
            await using (NpgsqlCommand timeout = new("SET LOCAL lock_timeout='1500ms'; SET LOCAL statement_timeout='3000ms'", connection, transaction))
                await timeout.ExecuteNonQueryAsync(ct);
            await using (NpgsqlCommand gate = new("SELECT pg_advisory_xact_lock(hashtextextended(@key, 0))", connection, transaction))
            {
                gate.Parameters.AddWithValue("key", "ops_action:" + command.RequestId.ToString("D"));
                await gate.ExecuteNonQueryAsync(ct);
            }
            await using (NpgsqlCommand kind = new("SELECT target_scope FROM ops_actions WHERE request_id=@id", connection, transaction))
            {
                kind.Parameters.AddWithValue("id",command.RequestId);
                if (await kind.ExecuteScalarAsync(ct) is string scope && scope != "Character") return Fail(CompensationFailures.RequestConflict);
            }
            Stored? existing = await FindStored(connection, transaction, command.RequestId, ct);
            if (existing is not null)
            {
                if (!await Consistent(connection, transaction, existing, ct)) return Fail(CompensationFailures.Inconsistent);
                return existing.Hash == command.InputHash ? Result<ActionCommit>.Success(new("Replayed", existing.Receipt))
                    : Fail(CompensationFailures.RequestConflict);
            }
            if (await PostgresItemRepository.ReadOperationsReceiptAsync(connection, transaction, command.RequestId, ct) is not null)
                return Fail(CompensationFailures.Inconsistent);
            var instant = clock.GetUtcNow();
            var now = new DateTimeOffset(instant.UtcTicks - instant.UtcTicks % 10, TimeSpan.Zero); // PostgreSQL microsecond precision, including first response.
            if (CompensationFailures.CheckNew(command, policy, now) is { } error) return Fail(error);
            var intent = command.Preview.Intent;
            await using (NpgsqlCommand character = new("SELECT character_id FROM characters WHERE character_id=@id FOR UPDATE", connection, transaction))
            {
                character.Parameters.AddWithValue("id", intent.CharacterId);
                if (await character.ExecuteScalarAsync(ct) is null) return Fail(Failures.CharacterMissing);
            }
            // Reuse gameplay owner -> item request lock order. Never commit an item separately from the audit.
            if (await PostgresItemRepository.LockOperationsReceiptAsync(connection, transaction, intent.CharacterId, command.RequestId, ct) is not null)
                return Fail(CompensationFailures.Inconsistent);
            await using (NpgsqlCommand priorCase = new("SELECT action_request_id FROM ops_compensation_cases WHERE case_id=@case AND target_scope='Character' AND target_id=@id", connection, transaction))
            {
                priorCase.Parameters.AddWithValue("case", intent.CaseId);
                priorCase.Parameters.AddWithValue("id", intent.CharacterId);
                if (await priorCase.ExecuteScalarAsync(ct) is not null) return Fail(CompensationFailures.CaseConflict);
            }
            if (await Busy(connection, transaction, intent.CharacterId, now, ct)) return Fail(CompensationFailures.Busy);
            // Recheck expiry after acquiring all locks; first execution may have waited.
            if (CompensationFailures.CheckNew(command, policy, clock.GetUtcNow()) is { } freshError) return Fail(freshError);
            var item = await items.CommitInTransactionAsync(connection, transaction, CompensationCatalog.Create(command), now, ct);
            var action = CompensationCatalog.Receipt(command, item);
            await Insert(connection, transaction, command, action, ct);
            committing = true;
            await transaction.CommitAsync(ct);
            return Result<ActionCommit>.Success(new("Committed", action));
        }
        catch (PostgresException e) when (!committing && e.SqlState == PostgresErrorCodes.LockNotAvailable)
        { return Fail(CompensationFailures.Busy); }
        catch (PostgresException e) when (!committing && e.SqlState == PostgresErrorCodes.UniqueViolation
            && e.ConstraintName == "ops_compensation_cases_pkey")
        { return Fail(CompensationFailures.CaseConflict); }
        catch (Exception e) when (e is NpgsqlException or TimeoutException or OperationCanceledException)
        { return Fail(committing ? CompensationFailures.Unknown : Failures.StorageUnavailable); }
        catch (JsonException) { return Fail(CompensationFailures.Inconsistent); }
    }
    private static Result<ActionCommit> Fail(Failure error) => Result<ActionCommit>.Fail(error);
    private static async Task<bool> Busy(NpgsqlConnection connection, NpgsqlTransaction transaction, Guid id, DateTimeOffset now, CancellationToken ct)
    {
        await using (NpgsqlCommand lease = new("SELECT expires_at FROM character_session_leases WHERE character_id=@id FOR UPDATE NOWAIT", connection, transaction))
        {
            lease.Parameters.AddWithValue("id", id);
            if (await lease.ExecuteScalarAsync(ct) is DateTime expiry && expiry > now.UtcDateTime) return true;
        }
        await using (NpgsqlCommand sessions = new("""
            SELECT s.dungeon_session_id FROM dungeon_sessions s
            WHERE s.state IN ('Waiting','Loading','InProgress','SettlementPending')
              AND EXISTS (SELECT 1 FROM dungeon_session_members m WHERE m.dungeon_session_id=s.dungeon_session_id AND m.character_id=@id)
            ORDER BY s.dungeon_session_id FOR UPDATE OF s NOWAIT
            """, connection, transaction))
        {
            sessions.Parameters.AddWithValue("id", id);
            if (await sessions.ExecuteScalarAsync(ct) is not null) return true;
        }
        await using NpgsqlCommand settlement = new("""
            SELECT 1 FROM dungeon_reward_settlements WHERE state IN ('Pending','Processing')
              AND character_ids @> @ids LIMIT 1
            """, connection, transaction);
        settlement.Parameters.AddWithValue("ids", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(new[] { id }));
        return await settlement.ExecuteScalarAsync(ct) is not null;
    }
    private static async Task Insert(NpgsqlConnection connection, NpgsqlTransaction transaction, CompensationCommand input, OperationAction action, CancellationToken ct)
    {
        await using NpgsqlCommand command = new("""
            INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,case_id,target_scope,target_id,
                template_version,reason,committed_at,result_json,origin_http_request_id,origin_trace_id)
            VALUES(@request,@operation,@hash,@input,@operator,@case,'Character',@target,1,@reason,@at,@result,@http,@trace);
            INSERT INTO ops_compensation_cases(case_id,target_scope,target_id,action_request_id) VALUES(@case,'Character',@target,@request);
            """, connection, transaction);
        command.Parameters.AddWithValue("request", action.RequestId);
        command.Parameters.AddWithValue("operation", action.Operation);
        command.Parameters.AddWithValue("hash", Convert.FromHexString(input.InputHash));
        command.Parameters.AddWithValue("input", NpgsqlDbType.Jsonb, input.CanonicalInput);
        command.Parameters.AddWithValue("operator", action.OperatorId);
        command.Parameters.AddWithValue("case", action.CaseId);
        command.Parameters.AddWithValue("target", action.Target.Id);
        command.Parameters.AddWithValue("reason", action.Reason);
        command.Parameters.AddWithValue("at", action.CommittedAtUtc);
        command.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(action.Result, OperationsHttp.Json));
        command.Parameters.AddWithValue("http", action.OriginHttpRequestId);
        command.Parameters.AddWithValue("trace", NpgsqlDbType.Varchar, (object?)action.OriginTraceId ?? DBNull.Value);
        await command.ExecuteNonQueryAsync(ct);
    }
    public async Task<Result<OperationAction>> FindAsync(Guid requestId, CancellationToken ct)
    {
        try
        {
            await using var connection = await source.OpenConnectionAsync(ct);
            await using var transaction = await connection.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct);
            await using (NpgsqlCommand mode = new("SET TRANSACTION READ ONLY; SET LOCAL statement_timeout='3000ms'", connection, transaction)) await mode.ExecuteNonQueryAsync(ct);
            Stored? stored = await FindStored(connection, transaction, requestId, ct);
            if (stored is null) return Result<OperationAction>.Fail(CompensationFailures.Missing);
            return await Consistent(connection, transaction, stored, ct) ? Result<OperationAction>.Success(stored.Receipt)
                : Result<OperationAction>.Fail(CompensationFailures.Inconsistent);
        }
        catch (NpgsqlException) { return Result<OperationAction>.Fail(Failures.StorageUnavailable); }
        catch (JsonException) { return Result<OperationAction>.Fail(CompensationFailures.Inconsistent); }
    }
    public async Task<Result<Page<OperationAction>>> SearchAsync(Guid characterId, PageRequest page, CancellationToken ct)
    {
        try
        {
            await using var connection = await source.OpenConnectionAsync(ct);
            await using var transaction = await connection.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct);
            await using (NpgsqlCommand mode = new("SET TRANSACTION READ ONLY; SET LOCAL statement_timeout='3000ms'", connection, transaction)) await mode.ExecuteNonQueryAsync(ct);
            await using NpgsqlCommand query = new($"""
                SELECT {Columns} FROM ops_actions WHERE target_scope='Character' AND target_id=@id
                  {(page.After is null ? "" : "AND (committed_at,request_id)<(@at,@after)")}
                ORDER BY committed_at DESC,request_id DESC LIMIT @limit
                """, connection, transaction);
            query.Parameters.AddWithValue("id", characterId);
            query.Parameters.AddWithValue("limit", page.Limit + 1);
            if (page.After is { } after) { query.Parameters.AddWithValue("at", after.Timestamp!.Value); query.Parameters.AddWithValue("after", after.Id); }
            List<Stored> rows = [];
            await using (var reader = await query.ExecuteReaderAsync(ct)) while (await reader.ReadAsync(ct)) rows.Add(Read(reader));
            foreach (var stored in rows)
                if (!await Consistent(connection, transaction, stored, ct)) return Result<Page<OperationAction>>.Fail(CompensationFailures.Inconsistent);
            return Result<Page<OperationAction>>.Success(ReadMapping.Page(rows.Select(r => r.Receipt).ToArray(), page.Limit, a => new(a.RequestId, a.CommittedAtUtc)));
        }
        catch (NpgsqlException) { return Result<Page<OperationAction>>.Fail(Failures.StorageUnavailable); }
        catch (JsonException) { return Result<Page<OperationAction>>.Fail(CompensationFailures.Inconsistent); }
    }
}
