using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository
{
    public async Task<DungeonSession> SetDungeonDepartureAsync(Guid dungeonSessionId, string steamId, Guid characterId,
        Guid requestId, long expectedRevision, bool cancel, int minimumPartySize, DateTimeOffset now, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        if (!cancel) await PostgresAdmissionAccess.EnsureAsync(connection, transaction, steamId, connectionClock?.GetUtcNow() ?? now, cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(connection, transaction, now, cancellationToken);
        if (await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken) is null)
            throw new DungeonSessionNotFoundException(dungeonSessionId);
        DungeonSession session = (await ReadDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken))!;
        DungeonSession updated = DungeonDeparturePolicy.Apply(session, steamId, characterId, requestId,
            expectedRevision, cancel, minimumPartySize, now);
        if (!ReferenceEquals(session, updated))
            await WriteDungeonLobbyAsync(connection, transaction, updated, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return updated;
    }

    private static async Task WriteDungeonLobbyAsync(NpgsqlConnection connection, NpgsqlTransaction transaction,
        DungeonSession session, CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new("""
            UPDATE dungeon_sessions SET leader_character_id = @leader, lobby_revision = @revision,
                departure_request_id = @request, departure_request_revision = @request_revision,
                departure_requested_at = @requested_at, updated_at = @updated_at
            WHERE dungeon_session_id = @session;
            """, connection, transaction);
        command.Parameters.AddWithValue("session", session.DungeonSessionId);
        command.Parameters.Add("leader", NpgsqlDbType.Uuid).Value = (object?)session.LeaderCharacterId ?? DBNull.Value;
        command.Parameters.AddWithValue("revision", session.LobbyRevision);
        command.Parameters.Add("request", NpgsqlDbType.Uuid).Value = (object?)session.DepartureRequestId ?? DBNull.Value;
        command.Parameters.Add("request_revision", NpgsqlDbType.Bigint).Value = (object?)session.DepartureRequestRevision ?? DBNull.Value;
        command.Parameters.Add("requested_at", NpgsqlDbType.TimestampTz).Value = (object?)session.DepartureRequestedAt ?? DBNull.Value;
        command.Parameters.AddWithValue("updated_at", session.UpdatedAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }
}
