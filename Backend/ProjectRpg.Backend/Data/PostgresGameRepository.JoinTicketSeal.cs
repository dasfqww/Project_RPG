using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository
{
    public async Task<JoinTicketConsumptionResolution?> SealJoinTicketConsumptionAsync(
        string tokenHash, string serverId, Guid dungeonSessionId, Guid consumptionRequestId,
        string expectedSteamId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        if (consumptionRequestId == Guid.Empty) return null;
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        var session = await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken);
        now = connectionClock?.GetUtcNow() ?? now;
        if (session is null || session.Value.ServerId != serverId
            || session.Value.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress)) return null;

        NpgsqlCommand Command(string sql)
        {
            var command = new NpgsqlCommand(sql, connection, transaction);
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            command.Parameters.AddWithValue("server_id", serverId);
            command.Parameters.AddWithValue("request_id", consumptionRequestId);
            command.Parameters.AddWithValue("hash", tokenHash);
            command.Parameters.AddWithValue("steam_id", expectedSteamId);
            command.Parameters.AddWithValue("now", now);
            return command;
        }
        await using (var authority = Command("SELECT expires_at > @now FROM dungeon_sessions WHERE dungeon_session_id = @session_id;"))
            if (await authority.ExecuteScalarAsync(cancellationToken) is not true) return null;

        // Query by ID before filtering hash/peer: a collision must not be
        // mistaken for missing history and used to cancel a different ticket.
        await using (var receipt = Command("""
            SELECT token_hash, character_id, steam_id, connection_generation, consumed_at
            FROM game_join_ticket_consumptions
            WHERE dungeon_session_id = @session_id AND server_id = @server_id AND consumption_request_id = @request_id;
            """))
        await using (var reader = await receipt.ExecuteReaderAsync(cancellationToken))
        {
            if (await reader.ReadAsync(cancellationToken))
                return reader.GetString(0).TrimEnd() == tokenHash && reader.GetString(2) == expectedSteamId
                    ? new("Recorded", new(dungeonSessionId, serverId, consumptionRequestId,
                        reader.GetGuid(1), reader.GetString(2), reader.GetInt64(3), reader.GetFieldValue<DateTimeOffset>(4)), null)
                    : null;
        }
        await using (var cancellation = Command("""
            SELECT token_hash, expected_steam_id, cancelled_at
            FROM game_join_ticket_consumption_cancellations
            WHERE dungeon_session_id = @session_id AND server_id = @server_id AND consumption_request_id = @request_id;
            """))
        await using (var reader = await cancellation.ExecuteReaderAsync(cancellationToken))
        {
            if (await reader.ReadAsync(cancellationToken))
                return reader.GetString(0).TrimEnd() == tokenHash && reader.GetString(1) == expectedSteamId
                    ? new("Cancelled", null, new(dungeonSessionId, serverId, consumptionRequestId,
                        tokenHash, expectedSteamId, reader.GetFieldValue<DateTimeOffset>(2))) : null;
        }
        // Never classify a missing/legacy/purged ticket as unconsumed. The
        // DELETE is the positive proof; cancellation INSERT failure rolls it back.
        await using var seal = Command("""
            WITH cancelled AS (
                DELETE FROM game_join_tickets t
                WHERE t.token_hash = @hash AND t.dungeon_session_id = @session_id AND t.steam_id = @steam_id
                  AND NOT EXISTS (SELECT 1 FROM game_join_ticket_consumptions r WHERE r.token_hash = t.token_hash)
                  AND NOT EXISTS (SELECT 1 FROM game_join_ticket_consumption_cancellations c WHERE c.token_hash = t.token_hash)
                RETURNING t.token_hash
            )
            INSERT INTO game_join_ticket_consumption_cancellations
                (token_hash, consumption_request_id, server_id, dungeon_session_id, expected_steam_id, cancelled_at)
            SELECT token_hash, @request_id, @server_id, @session_id, @steam_id, @now FROM cancelled
            RETURNING cancelled_at;
            """);
        DateTimeOffset persistedCancelledAt;
        await using (var reader = await seal.ExecuteReaderAsync(cancellationToken))
        {
            if (!await reader.ReadAsync(cancellationToken)) return null;
            // PostgreSQL stores microseconds. Return the stored value even on the
            // first response, so a later proof cannot differ by sub-microsecond ticks.
            persistedCancelledAt = reader.GetFieldValue<DateTimeOffset>(0);
        }
        await transaction.CommitAsync(cancellationToken);
        return new("Cancelled", null, new(dungeonSessionId, serverId, consumptionRequestId, tokenHash, expectedSteamId, persistedCancelledAt));
    }
}
