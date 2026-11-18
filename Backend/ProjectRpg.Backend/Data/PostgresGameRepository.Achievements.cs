using System.Data;
using Npgsql;
using ProjectRpg.Backend.Achievements;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : IAchievementRepository
{
    public async Task<AchievementSnapshot?> ReadAchievementsAsync(Guid characterId, string steamId, CancellationToken ct)
    {
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await connection.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct);
        await using var owner = new NpgsqlCommand(
            "SELECT roster_id FROM characters WHERE character_id = @id AND steam_id = @owner", connection, tx);
        owner.Parameters.AddWithValue("id", characterId);
        owner.Parameters.AddWithValue("owner", steamId);
        if (await owner.ExecuteScalarAsync(ct) is not Guid roster) return null;

        var facts = new Dictionary<AchievementTrack, DateTimeOffset[]>();
        foreach (var (track, limit) in AchievementCatalog.Current.Thresholds)
        {
            await using var command = new NpgsqlCommand("""
                SELECT earned_at FROM roster_achievement_facts WHERE roster_id = @roster
                    AND kind = @kind AND subject = @subject AND difficulty = @difficulty
                ORDER BY earned_at, source_id LIMIT @limit
                """, connection, tx);
            command.Parameters.AddWithValue("roster", roster);
            command.Parameters.AddWithValue("kind", track.Kind);
            command.Parameters.AddWithValue("subject", track.Subject);
            command.Parameters.AddWithValue("difficulty", track.Difficulty);
            command.Parameters.AddWithValue("limit", limit);
            var times = new List<DateTimeOffset>();
            await using var reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct)) times.Add(reader.GetFieldValue<DateTimeOffset>(0));
            facts.Add(track, times.ToArray());
        }
        await tx.CommitAsync(ct);
        return AchievementCatalog.Current.Project(characterId, roster, facts);
    }
}
