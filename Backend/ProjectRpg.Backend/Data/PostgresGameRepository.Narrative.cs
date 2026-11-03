using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : INarrativeRepository
{
    private static readonly JsonSerializerOptions NarrativeJson = new(JsonSerializerDefaults.Web);

    private static NarrativeState ReadNarrative(string json, Guid characterId)
    {
        try
        {
            NarrativeState? state = JsonSerializer.Deserialize<NarrativeState>(json, NarrativeJson);
            if (state is null || state.CharacterId != characterId)
                throw new NarrativeConflictException("invalid_narrative_state");
            return state;
        }
        catch (JsonException) { throw new NarrativeConflictException("invalid_narrative_state"); }
    }

    private static async Task LockNarrativeOwner(NpgsqlConnection connection, NpgsqlTransaction transaction,
        Guid characterId, string steamId, CancellationToken cancellationToken)
    {
        await using NpgsqlCommand owner = new("""
            SELECT character_id FROM characters WHERE character_id = @id AND steam_id = @owner FOR UPDATE;
            """, connection, transaction);
        owner.Parameters.AddWithValue("id", characterId);
        owner.Parameters.AddWithValue("owner", steamId);
        if (await owner.ExecuteScalarAsync(cancellationToken) is not Guid)
            throw new NarrativeConflictException("character_not_owned");
    }

    private static async Task<NarrativeState> ReadNarrativeInTransaction(NpgsqlConnection connection,
        NpgsqlTransaction transaction, Guid characterId, CancellationToken cancellationToken)
    {
        await using NpgsqlCommand query = new(
            "SELECT state::TEXT FROM character_narrative WHERE character_id = @id;", connection, transaction);
        query.Parameters.AddWithValue("id", characterId);
        return await query.ExecuteScalarAsync(cancellationToken) is string json
            ? ReadNarrative(json, characterId) : NarrativeState.Create(characterId);
    }

    public async Task<NarrativeState> LoadNarrativeAsync(Guid characterId, string steamId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await LockNarrativeOwner(connection, transaction, characterId, steamId, cancellationToken);
        NarrativeState state = await ReadNarrativeInTransaction(connection, transaction, characterId, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return state;
    }

    public async Task<NarrativeState> CommitNarrativeAsync(Guid characterId, string steamId, NarrativeCommand command,
        Func<NarrativeState, NarrativeState> transition, DateTimeOffset now, CancellationToken cancellationToken)
    {
        string fingerprint = command.Fingerprint();
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await LockNarrativeOwner(connection, transaction, characterId, steamId, cancellationToken);
        // The character row also serializes the first insert and simultaneous distinct commands.
        await using (NpgsqlCommand query = new("""
            SELECT fingerprint, result::TEXT FROM character_narrative_commands
            WHERE character_id = @id AND request_id = @request;
            """, connection, transaction))
        {
            query.Parameters.AddWithValue("id", characterId);
            query.Parameters.AddWithValue("request", command.RequestId);
            await using NpgsqlDataReader reader = await query.ExecuteReaderAsync(cancellationToken);
            if (await reader.ReadAsync(cancellationToken))
            {
                if (reader.GetString(0) != fingerprint) throw new NarrativeConflictException("idempotency_conflict");
                return ReadNarrative(reader.GetString(1), characterId);
            }
        }
        NarrativeState state = await ReadNarrativeInTransaction(connection, transaction, characterId, cancellationToken);
        if (state.Revision != command.ExpectedRevision) throw new NarrativeConflictException("narrative_revision_conflict");
        NarrativeState updated = transition(state);
        if (updated.CharacterId != characterId || updated.Revision != state.Revision + 1)
            throw new NarrativeConflictException("invalid_narrative_transition");
        string result = JsonSerializer.Serialize(updated, NarrativeJson);
        await using (NpgsqlCommand save = new("""
            INSERT INTO character_narrative (character_id, state, updated_at) VALUES (@id, @state, @now)
            ON CONFLICT (character_id) DO UPDATE SET state = EXCLUDED.state, updated_at = EXCLUDED.updated_at;
            INSERT INTO character_narrative_commands (character_id, request_id, fingerprint, result, committed_at)
            VALUES (@id, @request, @fingerprint, @state, @now);
            """, connection, transaction))
        {
            save.Parameters.AddWithValue("id", characterId);
            save.Parameters.AddWithValue("request", command.RequestId);
            save.Parameters.AddWithValue("fingerprint", fingerprint);
            save.Parameters.AddWithValue("state", NpgsqlDbType.Jsonb, result);
            save.Parameters.AddWithValue("now", now);
            await save.ExecuteNonQueryAsync(cancellationToken);
        }
        await transaction.CommitAsync(cancellationToken);
        return updated;
    }
}
