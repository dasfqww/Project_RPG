using Npgsql;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : INarrativeDungeonEvidenceRepository
{
    public async Task<NarrativeDungeonClearEvidence?> LoadNarrativeDungeonClearEvidenceAsync(
        Guid dungeonSessionId, Guid characterId, CancellationToken cancellationToken)
    {
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand query = new("""
            SELECT r.server_id, c.steam_id, s.dungeon_id, s.difficulty, r.created_at
            FROM dungeon_reward_settlements r
            JOIN dungeon_sessions s ON s.dungeon_session_id = r.dungeon_session_id
            JOIN characters c ON c.character_id = @character
            WHERE r.dungeon_session_id = @session
              AND r.character_ids @> jsonb_build_array(c.character_id::text);
            """, connection);
        query.Parameters.AddWithValue("character", characterId);
        query.Parameters.AddWithValue("session", dungeonSessionId);
        await using var reader = await query.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken)) return null;
        return new(dungeonSessionId, reader.GetString(0), characterId, reader.GetString(1),
            reader.GetString(2), reader.GetString(3), reader.GetFieldValue<DateTimeOffset>(4));
    }
}
