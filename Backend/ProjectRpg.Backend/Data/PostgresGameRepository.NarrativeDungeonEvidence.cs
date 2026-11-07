using Npgsql;
using NpgsqlTypes;
using System.Collections.Immutable;
using System.Text.Json;
using System.Globalization;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : INarrativeDungeonEvidenceRepository
{
    public async Task<ImmutableArray<DungeonObjectiveCandidate>> FindNarrativeDungeonObjectiveCandidatesAsync(
        Guid characterId, string steamId, IReadOnlyList<NarrativeDungeonObjectiveQuery> queries, CancellationToken cancellationToken)
    {
        if (queries.Count == 0) return [];
        // One bounded query, including when the player's history has many unrelated results.
        string criteria = JsonSerializer.Serialize(queries.Select(q => new { quest_id = q.QuestId, objective_id = q.ObjectiveId,
            dungeon_id = q.DungeonId, difficulty = q.Difficulty,
            // PG stores microseconds, while narrative JSON preserves .NET ticks. Never round activation down.
            activated_at = q.ActivatedAt.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss.ffffff'Z'", CultureInfo.InvariantCulture),
            submicro = q.ActivatedAt.Ticks % 10 != 0, excluded = q.ExcludedSessionIds }));
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand query = new("""
            SELECT q.quest_id, q.objective_id, e.dungeon_session_id
            FROM jsonb_to_recordset(@criteria) AS q(quest_id text, objective_id text, dungeon_id text,
                difficulty text, activated_at timestamptz, submicro boolean, excluded jsonb)
            CROSS JOIN LATERAL (
                SELECT r.dungeon_session_id
                FROM dungeon_reward_settlements r
                JOIN dungeon_sessions s ON s.dungeon_session_id = r.dungeon_session_id
                JOIN characters c ON c.character_id = @character AND c.steam_id = @owner
                WHERE r.character_ids @> jsonb_build_array(c.character_id::text)
                  AND r.created_at >= q.activated_at AND (NOT q.submicro OR r.created_at > q.activated_at)
                  AND s.dungeon_id = q.dungeon_id AND s.difficulty = q.difficulty
                  AND NOT (q.excluded @> jsonb_build_array(r.dungeon_session_id::text))
                ORDER BY r.created_at, r.dungeon_session_id LIMIT 1
            ) e
            ORDER BY q.quest_id COLLATE "C", q.objective_id COLLATE "C" LIMIT 256;
            """, connection);
        query.Parameters.AddWithValue("criteria", NpgsqlDbType.Jsonb, criteria);
        query.Parameters.AddWithValue("character", characterId); query.Parameters.AddWithValue("owner", steamId);
        await using var reader = await query.ExecuteReaderAsync(cancellationToken);
        var candidates = ImmutableArray.CreateBuilder<DungeonObjectiveCandidate>();
        while (await reader.ReadAsync(cancellationToken)) candidates.Add(new(reader.GetString(0), reader.GetString(1), reader.GetGuid(2)));
        return candidates.ToImmutable();
    }

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
