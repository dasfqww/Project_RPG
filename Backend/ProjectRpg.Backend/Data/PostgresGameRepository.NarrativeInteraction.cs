using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : INarrativeInteractionRepository
{
    public async Task<NarrativeState> CommitNarrativeInteractionAsync(Guid sessionId, Guid characterId, string serverId,
        NarrativeInteractionCommand command, Func<NarrativeState, NarrativeInteractionContext, NarrativeState> transition,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        string fingerprint = command.Fingerprint(sessionId, characterId);
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using var tx = await connection.BeginTransactionAsync(cancellationToken);
        string dungeon, difficulty, state;
        DateTimeOffset sessionExpiry;
        // Fence finish/abandon/reconnect before locking membership and its character lease.
        await using (NpgsqlCommand session = new("""
            SELECT dungeon_id, difficulty, state, expires_at FROM dungeon_sessions
            WHERE dungeon_session_id=@session AND server_id=@server FOR UPDATE;
            """, connection, tx))
        {
            session.Parameters.AddWithValue("session", sessionId); session.Parameters.AddWithValue("server", serverId);
            await using var reader = await session.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken)) throw new NarrativeConflictException("interaction_not_authorized");
            dungeon = reader.GetString(0); difficulty = reader.GetString(1); state = reader.GetString(2);
            sessionExpiry = reader.GetFieldValue<DateTimeOffset>(3);
        }
        DateTimeOffset? memberExpiry = null, leaseExpiry = null, joinedAt = null;
        bool connected = false; long generation = 0, confirmed = 0;
        string? memberOwner = null;
        await using (NpgsqlCommand member = new("""
            SELECT m.lease_expires_at, l.expires_at, m.joined_at, m.disconnected_at IS NULL,
                   m.connection_generation, m.confirmed_connection_generation, m.steam_id
            FROM dungeon_session_members m JOIN character_session_leases l
              ON l.character_id=m.character_id AND l.dungeon_session_id=m.dungeon_session_id
            WHERE m.dungeon_session_id=@session AND m.character_id=@character FOR UPDATE OF m,l;
            """, connection, tx))
        {
            member.Parameters.AddWithValue("session", sessionId); member.Parameters.AddWithValue("character", characterId);
            await using var reader = await member.ExecuteReaderAsync(cancellationToken);
            if (await reader.ReadAsync(cancellationToken))
            {
                memberExpiry = reader.GetFieldValue<DateTimeOffset>(0); leaseExpiry = reader.GetFieldValue<DateTimeOffset>(1);
                joinedAt = reader.GetFieldValue<DateTimeOffset>(2); connected = reader.GetBoolean(3);
                generation = reader.GetInt64(4); confirmed = reader.GetInt64(5);
                memberOwner = reader.GetString(6);
            }
        }
        string characterOwner;
        await using (NpgsqlCommand owner = new("SELECT steam_id FROM characters WHERE character_id=@id FOR UPDATE;", connection, tx))
        {
            owner.Parameters.AddWithValue("id", characterId);
            if (await owner.ExecuteScalarAsync(cancellationToken) is not string steamId) throw new NarrativeConflictException("interaction_not_authorized");
            characterOwner = steamId;
        }
        await using (NpgsqlCommand receipt = new("""
            SELECT fingerprint, result::text FROM character_narrative_commands WHERE character_id=@id AND request_id=@event;
            """, connection, tx))
        {
            receipt.Parameters.AddWithValue("id", characterId); receipt.Parameters.AddWithValue("event", command.EventId);
            await using var reader = await receipt.ExecuteReaderAsync(cancellationToken);
            if (await reader.ReadAsync(cancellationToken))
            {
                if (reader.GetString(0) != fingerprint) throw new NarrativeConflictException("idempotency_conflict");
                return ReadNarrative(reader.GetString(1), characterId);
            }
        }
        now = connectionClock?.GetUtcNow() ?? now;
        if (state != "InProgress" || sessionExpiry <= now || memberExpiry is null || memberExpiry <= now || leaseExpiry <= now || memberOwner != characterOwner
            || !connected || generation != command.ConnectionGeneration || confirmed != command.ConnectionGeneration
            || command.ObservedAtUtc < joinedAt)
            throw new NarrativeConflictException("interaction_not_authorized");
        var current = await ReadNarrativeInTransaction(connection, tx, characterId, cancellationToken);
        var next = transition(current, new(dungeon, difficulty, now));
        if (next.CharacterId != characterId || next.Revision != current.Revision && next.Revision != current.Revision + 1
            || next.Revision == current.Revision && !ReferenceEquals(current, next))
            throw new NarrativeConflictException("invalid_narrative_transition");
        string result = JsonSerializer.Serialize(next, NarrativeJson);
        await using NpgsqlCommand save = new("""
            INSERT INTO character_narrative(character_id,state,updated_at) VALUES (@id,@result,@now)
              ON CONFLICT(character_id) DO UPDATE SET state=EXCLUDED.state,updated_at=EXCLUDED.updated_at;
            INSERT INTO character_narrative_commands(character_id,request_id,fingerprint,result,committed_at)
              VALUES (@id,@event,@fingerprint,@result,@now);
            """, connection, tx);
        save.Parameters.AddWithValue("id", characterId); save.Parameters.AddWithValue("event", command.EventId);
        save.Parameters.AddWithValue("fingerprint", fingerprint); save.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, result);
        save.Parameters.AddWithValue("now", now);
        await save.ExecuteNonQueryAsync(cancellationToken); await tx.CommitAsync(cancellationToken);
        return next;
    }
}
