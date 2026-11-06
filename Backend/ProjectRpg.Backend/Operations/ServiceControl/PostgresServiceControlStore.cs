using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.ServiceControl;

namespace ProjectRpg.Backend.Operations.ServiceControl;

public sealed class PostgresServiceControlStore(NpgsqlDataSource source, TimeProvider clock) : IServiceControlReader, IServiceControlCommitter
{
    public async Task<Result<ServiceSnapshot>> ReadAsync(CancellationToken ct)
    {
        await using var connection = await source.OpenConnectionAsync(ct);
        await using var query = new NpgsqlCommand("SELECT revision,mode,notice_text,notice_starts_at,notice_ends_at,updated_at,action_request_id FROM ops_service_control WHERE id=1", connection) { CommandTimeout = 3 };
        await using var r = await query.ExecuteReaderAsync(ct);
        if (!await r.ReadAsync(ct)) return Result<ServiceSnapshot>.Fail(Failures.StorageUnavailable);
        return Result<ServiceSnapshot>.Success(new(r.GetInt64(0), r.GetString(1), r.GetString(2),
            r.IsDBNull(3) ? null : r.GetFieldValue<DateTimeOffset>(3), r.IsDBNull(4) ? null : r.GetFieldValue<DateTimeOffset>(4),
            r.IsDBNull(5) ? null : r.GetFieldValue<DateTimeOffset>(5), r.IsDBNull(6) ? null : r.GetGuid(6)));
    }
    public async Task<Result<ServiceReceipt>> FindActionAsync(Guid id, CancellationToken ct)
    {
        await using var connection = await source.OpenConnectionAsync(ct);
        await using var query = new NpgsqlCommand("SELECT result_json::text FROM ops_actions WHERE request_id=@id AND target_scope='Service'", connection) { CommandTimeout = 3 };
        query.Parameters.AddWithValue("id", id);
        var json = await query.ExecuteScalarAsync(ct) as string;
        return json is null ? Result<ServiceReceipt>.Fail(ServiceFailures.Missing)
            : Result<ServiceReceipt>.Success(JsonSerializer.Deserialize<ServiceReceipt>(json, OperationsHttp.Json) ?? throw new JsonException());
    }
    public async Task<Result<ServiceCommit>> CommitAsync(ServiceCommand command, CancellationToken ct)
    {
        bool committing = false;
        try
        {
            await using var connection = await source.OpenConnectionAsync(ct);
            await using var tx = await connection.BeginTransactionAsync(ct);
            await using (var gate = new NpgsqlCommand("SET LOCAL statement_timeout='3000ms'; SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", connection, tx))
            { gate.Parameters.AddWithValue("key", "ops_action:" + command.Intent.RequestId.ToString("D")); await gate.ExecuteNonQueryAsync(ct); }
            await using (var prior = new NpgsqlCommand("SELECT target_scope,input_hash,result_json::text FROM ops_actions WHERE request_id=@id", connection, tx))
            {
                prior.Parameters.AddWithValue("id", command.Intent.RequestId);
                await using var r = await prior.ExecuteReaderAsync(ct);
                if (await r.ReadAsync(ct))
                {
                    if (r.GetString(0) != "Service" || Convert.ToHexString(r.GetFieldValue<byte[]>(1)) != command.InputHash)
                        return Result<ServiceCommit>.Fail(ServiceFailures.Conflict);
                    return Result<ServiceCommit>.Success(new("Replayed", JsonSerializer.Deserialize<ServiceReceipt>(r.GetString(2), OperationsHttp.Json) ?? throw new JsonException()));
                }
            }
            await PostgresServiceAccess.LockAsync(connection, tx, true, ct);
            await using (var current = new NpgsqlCommand("SELECT revision FROM ops_service_control WHERE id=1 FOR UPDATE", connection, tx))
            {
                var revision = await current.ExecuteScalarAsync(ct);
                if (revision is not long value) return Result<ServiceCommit>.Fail(Failures.StorageUnavailable);
                if (value != command.Intent.ExpectedRevision) return Result<ServiceCommit>.Fail(ServiceFailures.Revision);
            }
            var instant = clock.GetUtcNow();
            var now = new DateTimeOffset(instant.UtcTicks - instant.UtcTicks % 10, TimeSpan.Zero);
            var receipt = command.Receipt(now);
            await using (var write = new NpgsqlCommand("""
                INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,target_scope,reason,committed_at,result_json,origin_http_request_id,origin_trace_id)
                VALUES(@id,@operation,@hash,@input,@operator,'Service',@reason,@now,@result,@http,@trace);
                UPDATE ops_service_control SET revision=@revision,mode=@mode,notice_text=@notice,notice_starts_at=@starts,
                    notice_ends_at=@ends,updated_at=@now,action_request_id=@id WHERE id=1;
                """, connection, tx))
            {
                write.Parameters.AddWithValue("id", receipt.RequestId); write.Parameters.AddWithValue("operation", ServiceCommand.Operation);
                write.Parameters.AddWithValue("hash", Convert.FromHexString(command.InputHash)); write.Parameters.AddWithValue("input", NpgsqlDbType.Jsonb, command.CanonicalInput);
                write.Parameters.AddWithValue("operator", command.OperatorId); write.Parameters.AddWithValue("reason", receipt.Reason);
                write.Parameters.AddWithValue("now", now); write.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(receipt, OperationsHttp.Json));
                write.Parameters.AddWithValue("http", command.OriginHttpRequestId); write.Parameters.AddWithValue("trace", NpgsqlDbType.Varchar, (object?)command.OriginTraceId ?? DBNull.Value);
                write.Parameters.AddWithValue("revision", receipt.Result.Revision); write.Parameters.AddWithValue("mode", receipt.Result.Mode); write.Parameters.AddWithValue("notice", receipt.Result.NoticeText);
                write.Parameters.AddWithValue("starts", NpgsqlDbType.TimestampTz, (object?)receipt.Result.NoticeStartsAtUtc ?? DBNull.Value);
                write.Parameters.AddWithValue("ends", NpgsqlDbType.TimestampTz, (object?)receipt.Result.NoticeEndsAtUtc ?? DBNull.Value);
                await write.ExecuteNonQueryAsync(ct);
            }
            committing = true; await tx.CommitAsync(ct);
            return Result<ServiceCommit>.Success(new("Committed", receipt));
        }
        catch (Exception e) when (e is NpgsqlException or TimeoutException or OperationCanceledException)
        { return Result<ServiceCommit>.Fail(committing ? ServiceFailures.Unknown : Failures.StorageUnavailable); }
    }
}
