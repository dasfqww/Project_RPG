using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository
{
    private static async Task<string?> PickupMemberAsync(NpgsqlConnection connection, NpgsqlTransaction transaction,
        Guid sessionId, string serverId, Guid characterId, DateTimeOffset now, CancellationToken cancellation)
    {
        await using var command = new NpgsqlCommand("""
            SELECT m.steam_id FROM dungeon_session_members m
            JOIN characters c ON c.character_id = m.character_id AND c.steam_id = m.steam_id
            JOIN character_session_leases l ON l.character_id = m.character_id AND l.dungeon_session_id = m.dungeon_session_id
            JOIN dungeon_sessions s ON s.dungeon_session_id = m.dungeon_session_id
            WHERE m.dungeon_session_id = @session AND m.character_id = @character
              AND s.server_id = @server AND s.state = 'InProgress' AND s.expires_at > @now
              AND m.lease_expires_at > @now AND l.expires_at > @now
              AND NOT EXISTS (SELECT 1 FROM dungeon_session_abandonments a
                  WHERE a.dungeon_session_id = m.dungeon_session_id AND a.character_id = m.character_id);
            """, connection, transaction);
        command.Parameters.AddWithValue("session", sessionId); command.Parameters.AddWithValue("character", characterId);
        command.Parameters.AddWithValue("server", serverId); command.Parameters.AddWithValue("now", now);
        return await command.ExecuteScalarAsync(cancellation) as string;
    }

    private static DungeonPickupIntent ReadPickup(NpgsqlDataReader reader) => new(reader.GetGuid(0), reader.GetString(1),
        reader.GetGuid(2), reader.GetGuid(3), reader.GetGuid(4), reader.GetString(5), reader.GetGuid(6),
        DungeonPickupIntentRules.Canonicalize(reader.GetString(7)), reader.GetFieldValue<DateTimeOffset>(8),
        reader.IsDBNull(9) ? null : reader.GetFieldValue<DateTimeOffset>(9));

    internal async Task<DungeonPickupCommitResult> CommitPickupAsync(PostgresItemRepository items, Guid sessionId,
        string serverId, Guid requestId, TimeProvider clock, CancellationToken cancellation)
    {
        await using var connection = await dataSource.OpenConnectionAsync(cancellation);
        await using var transaction = await connection.BeginTransactionAsync(cancellation);
        try
        {
            var session = await LockDungeonSessionAsync(connection, transaction, sessionId, cancellation);
            if (session is null || session.Value.ServerId != serverId || session.Value.State != DungeonSessionState.InProgress)
                return new(DungeonPickupCommitStatus.Forbidden);
            await using (var expiry = new NpgsqlCommand("SELECT expires_at > @now FROM dungeon_sessions WHERE dungeon_session_id = @id", connection, transaction))
            {
                expiry.Parameters.AddWithValue("id", sessionId); expiry.Parameters.AddWithValue("now", clock.GetUtcNow());
                if (await expiry.ExecuteScalarAsync(cancellation) is not true) return new(DungeonPickupCommitStatus.Forbidden);
            }
            DungeonPickupIntent intent;
            await using (var read = new NpgsqlCommand("""
                SELECT dungeon_session_id, server_id, drop_id, request_id, character_id, steam_id, item_id,
                    command_payload::text, prepared_at, consumed_at
                FROM dungeon_pickup_intents WHERE dungeon_session_id = @session AND request_id = @request;
                """, connection, transaction))
            {
                read.Parameters.AddWithValue("session", sessionId); read.Parameters.AddWithValue("request", requestId);
                await using var reader = await read.ExecuteReaderAsync(cancellation);
                if (!await reader.ReadAsync(cancellation)) return new(DungeonPickupCommitStatus.NotFound);
                intent = ReadPickup(reader);
            }
            if (intent.ServerId != serverId) return new(DungeonPickupCommitStatus.Forbidden);
            async Task Authorize()
            {
                if (await PickupMemberAsync(connection, transaction, sessionId, serverId, intent.CharacterId,
                    clock.GetUtcNow(), cancellation) != intent.SteamId) throw new DungeonPickupAuthorityLostException();
            }
            await Authorize();
            var command = DungeonPickupIntentRules.RestoreCommand(intent);
            var result = await items.CommitPickupInTransactionAsync(connection, transaction, command,
                intent.ConsumedAt is not null, clock, Authorize, cancellation);
            if (result.Status is not (ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted))
                return DungeonPickupCommitResult.FromItem(result); // Dispose rolls back; reservation remains prepared.
            if (!DungeonPickupReceiptRules.Matches(command, result))
                throw new InvalidOperationException("Pickup receipt did not match its frozen origin.");
            if (intent.ConsumedAt is null)
            {
                await using var consume = new NpgsqlCommand("""
                    UPDATE dungeon_pickup_intents SET consumed_at = @at
                    WHERE dungeon_session_id = @session AND request_id = @request AND consumed_at IS NULL;
                    """, connection, transaction);
                consume.Parameters.AddWithValue("at", result.CommittedAt); consume.Parameters.AddWithValue("session", sessionId);
                consume.Parameters.AddWithValue("request", requestId);
                if (await consume.ExecuteNonQueryAsync(cancellation) != 1)
                    throw new InvalidOperationException("Pickup consumption failed.");
            }
            await transaction.CommitAsync(cancellation);
            return DungeonPickupCommitResult.FromItem(result);
        }
        catch (DungeonPickupAuthorityLostException) { return new(DungeonPickupCommitStatus.Forbidden); }
        catch (PostgresException error) when (error.SqlState == PostgresErrorCodes.UniqueViolation)
        {
            return new(error.ConstraintName == "ux_item_records_active_location"
                ? DungeonPickupCommitStatus.LocationConflict : DungeonPickupCommitStatus.RevisionConflict);
        }
    }

    public async Task<DungeonPickupIntentResult> PreparePickupAsync(Guid sessionId, string serverId, Guid dropId,
        ItemRepositoryCommitRequest command, DateTimeOffset now, CancellationToken cancellationToken)
    {
        if (!DungeonPickupIntentRules.ValidScope(sessionId, serverId)
            || !DungeonPickupIntentRules.TryFreeze(dropId, command, out var json))
            return new(DungeonPickupIntentStatus.InvalidRequest);
        command = DungeonPickupIntentRules.RestoreCommand(json);
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        // This is the same parent row lock used by opt-out, admission and settlement.
        var session = await LockDungeonSessionAsync(connection, transaction, sessionId, cancellationToken);
        if (session is null || session.Value.ServerId != serverId || session.Value.State != DungeonSessionState.InProgress)
            return new(DungeonPickupIntentStatus.Forbidden);
        Guid character = Guid.Parse(command.Actor.OwnerId);
        string? steam = await PickupMemberAsync(connection, transaction, sessionId, serverId, character, now, cancellationToken);
        if (steam is null) return new(DungeonPickupIntentStatus.Forbidden);
        Guid item = command.Mutations[0].NewRecord.ItemId;
        int inserted;
        await using (var insert = new NpgsqlCommand("""
            INSERT INTO dungeon_pickup_intents
                (dungeon_session_id, server_id, drop_id, request_id, character_id, steam_id, item_id, command_payload, prepared_at)
            VALUES (@session, @server, @drop, @request, @character, @steam, @item, @payload, @now)
            ON CONFLICT DO NOTHING;
            """, connection, transaction))
        {
            insert.Parameters.AddWithValue("session", sessionId); insert.Parameters.AddWithValue("server", serverId);
            insert.Parameters.AddWithValue("drop", dropId); insert.Parameters.AddWithValue("request", command.RequestId);
            insert.Parameters.AddWithValue("character", character); insert.Parameters.AddWithValue("steam", steam);
            insert.Parameters.AddWithValue("item", item); insert.Parameters.AddWithValue("payload", NpgsqlDbType.Jsonb, json);
            insert.Parameters.AddWithValue("now", now);
            inserted = await insert.ExecuteNonQueryAsync(cancellationToken);
        }
        List<DungeonPickupIntent> found = [];
        await using (var read = new NpgsqlCommand("""
            SELECT dungeon_session_id, server_id, drop_id, request_id, character_id, steam_id, item_id, command_payload::text, prepared_at, consumed_at
            FROM dungeon_pickup_intents WHERE request_id = @request OR item_id = @item
                OR (dungeon_session_id = @session AND drop_id = @drop);
            """, connection, transaction))
        {
            read.Parameters.AddWithValue("request", command.RequestId); read.Parameters.AddWithValue("item", item);
            read.Parameters.AddWithValue("session", sessionId); read.Parameters.AddWithValue("drop", dropId);
            await using var reader = await read.ExecuteReaderAsync(cancellationToken);
            while (await reader.ReadAsync(cancellationToken)) found.Add(ReadPickup(reader));
        }
        if (found.Count != 1 || found[0].SteamId != steam || !DungeonPickupIntentRules.Same(found[0], sessionId, serverId, dropId, json))
            return new(DungeonPickupIntentStatus.Conflict);
        await transaction.CommitAsync(cancellationToken);
        return new(inserted == 1 ? DungeonPickupIntentStatus.Prepared : DungeonPickupIntentStatus.AlreadyPrepared, found[0]);
    }

    public async Task<DungeonPickupIntentResult> ReadPickupIntentAsync(Guid sessionId, string serverId, Guid requestId,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        if (!DungeonPickupIntentRules.ValidScope(sessionId, serverId) || requestId == Guid.Empty)
            return new(DungeonPickupIntentStatus.InvalidRequest);
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using var transaction = await connection.BeginTransactionAsync(cancellationToken);
        var session = await LockDungeonSessionAsync(connection, transaction, sessionId, cancellationToken);
        if (session is null || session.Value.ServerId != serverId || session.Value.State != DungeonSessionState.InProgress)
            return new(DungeonPickupIntentStatus.Forbidden);
        // Check expiry even for unknown request IDs; do not expose a stale scope.
        await using (var expires = new NpgsqlCommand("SELECT expires_at > @now FROM dungeon_sessions WHERE dungeon_session_id = @session", connection, transaction))
        {
            expires.Parameters.AddWithValue("now", now); expires.Parameters.AddWithValue("session", sessionId);
            if (await expires.ExecuteScalarAsync(cancellationToken) is not true) return new(DungeonPickupIntentStatus.Forbidden);
        }
        DungeonPickupIntent intent;
        await using (var read = new NpgsqlCommand("""
            SELECT dungeon_session_id, server_id, drop_id, request_id, character_id, steam_id, item_id, command_payload::text, prepared_at, consumed_at
            FROM dungeon_pickup_intents WHERE dungeon_session_id = @session AND request_id = @request;
            """, connection, transaction))
        {
            read.Parameters.AddWithValue("session", sessionId); read.Parameters.AddWithValue("request", requestId);
            await using var reader = await read.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken)) return new(DungeonPickupIntentStatus.NotFound);
            intent = ReadPickup(reader);
        }
        if (intent.ServerId != serverId || await PickupMemberAsync(connection, transaction, sessionId, serverId, intent.CharacterId, now, cancellationToken) != intent.SteamId)
            return new(DungeonPickupIntentStatus.Forbidden);
        await transaction.CommitAsync(cancellationToken);
        return new(DungeonPickupIntentStatus.Prepared, intent);
    }
}

internal sealed class DungeonPickupAuthorityLostException : Exception;
