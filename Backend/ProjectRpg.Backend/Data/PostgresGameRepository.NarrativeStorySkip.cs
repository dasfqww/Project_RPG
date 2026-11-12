using System.Collections.Immutable;
using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : INarrativeStorySkipRepository
{
    private static async Task<NarrativeStorySkipContext> ReadStorySkipContext(NpgsqlConnection connection,
        NpgsqlTransaction tx, Guid characterId, string steamId, CancellationToken ct)
    {
        var states = ImmutableArray.CreateBuilder<NarrativeState>();
        await using (NpgsqlCommand query = new("""
            SELECT c.character_id, n.state::TEXT FROM characters c
            LEFT JOIN character_narrative n ON n.character_id=c.character_id
            WHERE c.steam_id=@owner ORDER BY c.character_id;
            """, connection, tx))
        {
            query.Parameters.AddWithValue("owner", steamId);
            await using var reader = await query.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                Guid id = reader.GetGuid(0);
                states.Add(reader.IsDBNull(1) ? NarrativeState.Create(id) : ReadNarrative(reader.GetString(1), id));
            }
        }
        // The selected owner FOR UPDATE fence also conflicts with membership/lease FK inserts.
        // Existing session writers hold their FK key-share until commit, so a concurrent
        // departure/admission cannot sneak an active membership past the owner fence.
        await using NpgsqlCommand busy = new("""
            SELECT EXISTS(SELECT 1 FROM dungeon_session_members m
                JOIN dungeon_sessions s ON s.dungeon_session_id=m.dungeon_session_id
                WHERE m.character_id=@id AND s.state NOT IN ('Cleared','Failed','Closed'));
            """, connection, tx);
        busy.Parameters.AddWithValue("id", characterId);
        return new(states.ToImmutable(), (bool)(await busy.ExecuteScalarAsync(ct))!);
    }
    public async Task<NarrativeStorySkipContext> LoadStorySkipContextAsync(Guid characterId, string steamId, CancellationToken ct)
    {
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await connection.BeginTransactionAsync(ct);
        await LockNarrativeOwner(connection, tx, characterId, steamId, ct);
        var result = await ReadStorySkipContext(connection, tx, characterId, steamId, ct);
        await tx.CommitAsync(ct); return result;
    }
    public async Task<NarrativeState> CommitStorySkipAsync(Guid characterId, string steamId, StorySkipCommand command,
        Func<NarrativeStorySkipContext, NarrativeState> transition, DateTimeOffset now, CancellationToken ct)
    {
        string fingerprint = command.Fingerprint();
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await connection.BeginTransactionAsync(ct);
        await LockNarrativeOwner(connection, tx, characterId, steamId, ct);
        await using (NpgsqlCommand query = new("""
            SELECT fingerprint, result::TEXT FROM character_narrative_commands
            WHERE character_id=@id AND request_id=@request;
            """, connection, tx))
        {
            query.Parameters.AddWithValue("id", characterId); query.Parameters.AddWithValue("request", command.RequestId);
            await using var reader = await query.ExecuteReaderAsync(ct);
            if (await reader.ReadAsync(ct))
            {
                if (reader.GetString(0) != fingerprint) throw new NarrativeConflictException("idempotency_conflict");
                return ReadNarrative(reader.GetString(1), characterId);
            }
        }
        var context = await ReadStorySkipContext(connection, tx, characterId, steamId, ct);
        var state = context.AccountStates.Single(s => s.CharacterId == characterId);
        if (state.Revision != command.ExpectedRevision) throw new NarrativeConflictException("narrative_revision_conflict");
        var updated = transition(context);
        if (updated.CharacterId != characterId || updated.Revision != checked(state.Revision + 1))
            throw new NarrativeConflictException("invalid_narrative_transition");
        string json = JsonSerializer.Serialize(updated, NarrativeJson);
        await using (NpgsqlCommand save = new("""
            INSERT INTO character_narrative(character_id,state,updated_at) VALUES(@id,@state,@now)
            ON CONFLICT(character_id) DO UPDATE SET state=EXCLUDED.state,updated_at=EXCLUDED.updated_at;
            INSERT INTO character_narrative_commands(character_id,request_id,fingerprint,result,committed_at)
            VALUES(@id,@request,@fingerprint,@state,@now);
            """, connection, tx))
        {
            save.Parameters.AddWithValue("id", characterId); save.Parameters.AddWithValue("request", command.RequestId);
            save.Parameters.AddWithValue("fingerprint", fingerprint); save.Parameters.AddWithValue("state", NpgsqlDbType.Jsonb, json);
            save.Parameters.AddWithValue("now", now); await save.ExecuteNonQueryAsync(ct);
        }
        await tx.CommitAsync(ct); return updated;
    }
}
