using Npgsql;

namespace ProjectRpg.Backend.Operations.Infrastructure.Authentication;

public sealed class PostgresOperatorSessionRepository(NpgsqlDataSource source) : IOperatorSessionRepository
{
    public async Task StoreAsync(OperatorSession session, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await source.OpenConnectionAsync(ct);
        await using NpgsqlCommand command = new("""
            INSERT INTO ops_operator_sessions
                (key_hash,operator_id,protected_ticket,auth_epoch,created_at,last_seen_at,absolute_expires_at)
            VALUES (@key,@operator,@ticket,@epoch,@created,@seen,@expires);
            """, connection) { CommandTimeout = 3 };
        command.Parameters.AddWithValue("key", session.KeyHash);
        command.Parameters.AddWithValue("operator", session.OperatorId);
        command.Parameters.AddWithValue("ticket", session.ProtectedTicket);
        command.Parameters.AddWithValue("epoch", session.AuthEpoch);
        command.Parameters.AddWithValue("created", session.CreatedAt);
        command.Parameters.AddWithValue("seen", session.LastSeenAt);
        command.Parameters.AddWithValue("expires", session.AbsoluteExpiresAt);
        await command.ExecuteNonQueryAsync(ct);
    }

    public async Task<OperatorSession?> TouchAsync(byte[] keyHash, string operatorId, string epoch, DateTimeOffset now, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await source.OpenConnectionAsync(ct);
        await using NpgsqlCommand command = new("""
            UPDATE ops_operator_sessions SET last_seen_at = @now
            WHERE key_hash=@key AND operator_id=@operator AND auth_epoch=@epoch AND revoked_at IS NULL
                AND absolute_expires_at > @now AND last_seen_at > @now - interval '30 minutes'
            RETURNING protected_ticket,created_at,absolute_expires_at;
            """, connection) { CommandTimeout = 3 };
        command.Parameters.AddWithValue("key", keyHash);
        command.Parameters.AddWithValue("operator", operatorId);
        command.Parameters.AddWithValue("epoch", epoch);
        command.Parameters.AddWithValue("now", now);
        await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct) ? new(keyHash, operatorId, reader.GetFieldValue<byte[]>(0), epoch,
            reader.GetFieldValue<DateTimeOffset>(1), now, reader.GetFieldValue<DateTimeOffset>(2)) : null;
    }

    public async Task RevokeAsync(byte[] keyHash, DateTimeOffset now, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await source.OpenConnectionAsync(ct);
        await using NpgsqlCommand command = new("UPDATE ops_operator_sessions SET revoked_at=@now WHERE key_hash=@key;", connection) { CommandTimeout = 3 };
        command.Parameters.AddWithValue("key", keyHash);
        command.Parameters.AddWithValue("now", now);
        await command.ExecuteNonQueryAsync(ct);
    }

    public async Task PurgeAsync(DateTimeOffset now, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await source.OpenConnectionAsync(ct);
        await using NpgsqlCommand command = new("""
            DELETE FROM ops_operator_sessions WHERE absolute_expires_at < @now - interval '24 hours'
                OR revoked_at < @now - interval '24 hours';
            """, connection) { CommandTimeout = 3 };
        command.Parameters.AddWithValue("now", now);
        await command.ExecuteNonQueryAsync(ct);
    }
}
