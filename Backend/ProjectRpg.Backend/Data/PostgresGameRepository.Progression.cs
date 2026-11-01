using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : ICharacterProgressionRepository
{
    private static readonly JsonSerializerOptions ProgressionJson = new(JsonSerializerDefaults.Web);

    public async Task<IReadOnlyDictionary<Guid, CharacterProgressionSummary>> GetStoredProgressionSummariesAsync(
        string steamId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand query = new("""
            SELECT owned.character_id, progression.state::TEXT
            FROM characters owned
            LEFT JOIN character_progression progression ON progression.character_id = owned.character_id
            WHERE owned.steam_id = @steam_id;
            """, connection);
        query.Parameters.AddWithValue("steam_id", steamId);
        await using NpgsqlDataReader reader = await query.ExecuteReaderAsync(cancellationToken);
        Dictionary<Guid, CharacterProgressionSummary> summaries = [];
        while (await reader.ReadAsync(cancellationToken))
        {
            Guid characterId = reader.GetGuid(0);
            if (reader.IsDBNull(1)) { summaries[characterId] = CharacterProgressionSummary.Missing; continue; }
            try
            {
                CharacterProgressionState? stored = JsonSerializer.Deserialize<CharacterProgressionState>(reader.GetString(1), ProgressionJson);
                summaries[characterId] = stored is null ? CharacterProgressionSummary.Unavailable
                    : CharacterProgressionSummary.FromStored(characterId, stored);
            }
            catch (JsonException) { summaries[characterId] = CharacterProgressionSummary.Unavailable; }
        }
        return summaries;
    }

    private static async Task InitializeProgressionAsync(NpgsqlConnection connection, NpgsqlTransaction transaction,
        Guid characterId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new("""
            INSERT INTO character_progression (character_id, state, updated_at)
            VALUES (@character_id, @state, @now);
            """, connection, transaction);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue("state", NpgsqlDbType.Jsonb,
            JsonSerializer.Serialize(FirstSliceProgression.Create(characterId), ProgressionJson));
        command.Parameters.AddWithValue("now", now);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    public async Task<CharacterProgressionState?> LoadProgressionAsync(Guid characterId, DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        CharacterProgressionState? state = await LoadProgressionInTransactionAsync(
            connection, transaction, characterId, null, now, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return state;
    }

    private static async Task<CharacterProgressionState?> LoadProgressionInTransactionAsync(
        NpgsqlConnection connection, NpgsqlTransaction transaction, Guid characterId, string? steamId,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        // Share the character FK lock with admission. A configuration write cannot
        // pass the lease check while a concurrent session admission is unpublished.
        await using (NpgsqlCommand owner = new("""
            SELECT character_id FROM characters
            WHERE character_id = @character_id AND (@steam_id::TEXT IS NULL OR steam_id = @steam_id)
            FOR UPDATE;
            """, connection, transaction))
        {
            owner.Parameters.AddWithValue("character_id", characterId);
            owner.Parameters.AddWithValue("steam_id", NpgsqlDbType.Text, (object?)steamId ?? DBNull.Value);
            if (await owner.ExecuteScalarAsync(cancellationToken) is not Guid)
            {
                if (steamId is not null) throw new ProgressionConflictException("character_not_owned");
                return null;
            }
        }

        CharacterProgressionState state;
        await using (NpgsqlCommand query = new("""
            SELECT state::TEXT FROM character_progression WHERE character_id = @character_id FOR UPDATE;
            """, connection, transaction))
        {
            query.Parameters.AddWithValue("character_id", characterId);
            if (await query.ExecuteScalarAsync(cancellationToken) is not string json) return null;
            state = ReadProgression(json, characterId, allowLegacyClass: true);
        }

        CharacterProgressionState originalState = state;
        state = FirstSliceProgression.MigrateStoredState(state);

        List<(Guid SessionId, string DungeonId, string Difficulty)> completed = [];
        await using (NpgsqlCommand query = new("""
            SELECT reward.dungeon_session_id, session.dungeon_id, session.difficulty
            FROM dungeon_reward_settlements reward
            JOIN dungeon_sessions session ON session.dungeon_session_id = reward.dungeon_session_id
            WHERE reward.state = 'Completed' AND reward.completed_at IS NOT NULL
              AND reward.reward_version = @reward_version
              AND reward.character_ids @> @member
              AND NOT EXISTS (
                  SELECT 1 FROM character_progression_awards award
                  WHERE award.character_id = @character_id
                    AND award.dungeon_session_id = reward.dungeon_session_id)
            ORDER BY reward.created_at, reward.dungeon_session_id;
            """, connection, transaction))
        {
            query.Parameters.AddWithValue("character_id", characterId);
            query.Parameters.AddWithValue("reward_version", FirstSliceProgression.RewardVersion);
            query.Parameters.AddWithValue("member", NpgsqlDbType.Jsonb,
                JsonSerializer.Serialize(new[] { characterId }, ProgressionJson));
            await using NpgsqlDataReader reader = await query.ExecuteReaderAsync(cancellationToken);
            while (await reader.ReadAsync(cancellationToken))
                completed.Add((reader.GetGuid(0), reader.GetString(1), reader.GetString(2)));
        }

        bool changed = state.Revision != originalState.Revision;
        foreach (var clear in completed)
        {
            if (!FirstSliceProgression.TryCredit(state, clear.DungeonId, clear.Difficulty,
                    FirstSliceProgression.RewardVersion, out CharacterProgressionState updated)) continue;
            await using NpgsqlCommand award = new("""
                INSERT INTO character_progression_awards (character_id, dungeon_session_id, credited_at)
                VALUES (@character_id, @session_id, @now);
                """, connection, transaction);
            award.Parameters.AddWithValue("character_id", characterId);
            award.Parameters.AddWithValue("session_id", clear.SessionId);
            award.Parameters.AddWithValue("now", now);
            await award.ExecuteNonQueryAsync(cancellationToken);
            changed |= updated.Revision != state.Revision;
            state = updated;
        }
        if (changed) await WriteProgressionAsync(connection, transaction, state, now, cancellationToken);
        return state;
    }

    public async Task<CharacterProgressionState> CommitSkillConfigurationAsync(Guid characterId, string steamId,
        SkillConfigurationCommand command, DateTimeOffset now, CancellationToken cancellationToken)
    {
        string fingerprint = FirstSliceProgression.Fingerprint(command);
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        CharacterProgressionState state = await LoadProgressionInTransactionAsync(
            connection, transaction, characterId, steamId, now, cancellationToken)
            ?? throw new ProgressionConflictException("progression_not_initialized");
        await using (NpgsqlCommand receipt = new("""
            SELECT fingerprint, result::TEXT FROM character_progression_commands
            WHERE character_id = @character_id AND request_id = @request_id;
            """, connection, transaction))
        {
            receipt.Parameters.AddWithValue("character_id", characterId);
            receipt.Parameters.AddWithValue("request_id", command.RequestId);
            await using NpgsqlDataReader reader = await receipt.ExecuteReaderAsync(cancellationToken);
            if (await reader.ReadAsync(cancellationToken))
            {
                if (reader.GetString(0) != fingerprint) throw new ProgressionConflictException("idempotency_conflict");
                CharacterProgressionState original = ReadProgression(reader.GetString(1), characterId, allowLegacyClass: true);
                await reader.DisposeAsync();
                await transaction.CommitAsync(cancellationToken);
                return original;
            }
        }
        await using (NpgsqlCommand lease = new("""
            SELECT EXISTS (SELECT 1 FROM character_session_leases
                WHERE character_id = @character_id AND expires_at > @now);
            """, connection, transaction))
        {
            lease.Parameters.AddWithValue("character_id", characterId);
            lease.Parameters.AddWithValue("now", now);
            if (await lease.ExecuteScalarAsync(cancellationToken) is true)
                throw new ProgressionConflictException("active_dungeon_configuration_locked");
        }
        CharacterProgressionState updated = FirstSliceProgression.Configure(state, command);
        await WriteProgressionAsync(connection, transaction, updated, now, cancellationToken);
        await using (NpgsqlCommand receipt = new("""
            INSERT INTO character_progression_commands (character_id, request_id, fingerprint, result, created_at)
            VALUES (@character_id, @request_id, @fingerprint, @result, @now);
            """, connection, transaction))
        {
            receipt.Parameters.AddWithValue("character_id", characterId);
            receipt.Parameters.AddWithValue("request_id", command.RequestId);
            receipt.Parameters.AddWithValue("fingerprint", fingerprint);
            receipt.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(updated, ProgressionJson));
            receipt.Parameters.AddWithValue("now", now);
            await receipt.ExecuteNonQueryAsync(cancellationToken);
        }
        await transaction.CommitAsync(cancellationToken);
        return updated;
    }

    private static CharacterProgressionState ReadProgression(string json, Guid characterId, bool allowLegacyClass = false)
    {
        CharacterProgressionState state = JsonSerializer.Deserialize<CharacterProgressionState>(json, ProgressionJson)
            ?? throw new ProgressionConflictException("invalid_progression_state");
        FirstSliceProgression.ValidateState(state, allowLegacyClass);
        if (state.CharacterId != characterId) throw new ProgressionConflictException("invalid_progression_owner");
        return state;
    }

    private static async Task WriteProgressionAsync(NpgsqlConnection connection, NpgsqlTransaction transaction,
        CharacterProgressionState state, DateTimeOffset now, CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new("""
            UPDATE character_progression SET state = @state, updated_at = @now WHERE character_id = @character_id;
            """, connection, transaction);
        command.Parameters.AddWithValue("character_id", state.CharacterId);
        command.Parameters.AddWithValue("state", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(state, ProgressionJson));
        command.Parameters.AddWithValue("now", now);
        if (await command.ExecuteNonQueryAsync(cancellationToken) != 1)
            throw new ProgressionConflictException("progression_not_initialized");
    }
}
