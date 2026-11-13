using Npgsql;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : IRidingAdmissionSnapshotStore
{
    public async Task<RidingAdmissionSnapshot?> GetRidingAdmissionSnapshotAsync(string serverId, Guid dungeonSessionId,
        Guid characterId, long connectionGeneration, CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(serverId) || dungeonSessionId == Guid.Empty || characterId == Guid.Empty || connectionGeneration <= 0) return null;
        await using var c = await dataSource.OpenConnectionAsync(cancellationToken);
        await using var tx = await c.BeginTransactionAsync(System.Data.IsolationLevel.RepeatableRead, cancellationToken);
        await using (var mode = new NpgsqlCommand("SET TRANSACTION READ ONLY", c, tx)) await mode.ExecuteNonQueryAsync(cancellationToken);
        Guid roster; DateTimeOffset observed, deadline;
        // One MVCC snapshot for membership and the complete roster/preferences read. No admission/lease updates or lock inversions.
        await using (var query = new NpgsqlCommand("""
            SELECT ch.roster_id,clock_timestamp(),LEAST(s.expires_at,m.lease_expires_at,l.expires_at)
            FROM dungeon_sessions s JOIN dungeon_session_members m USING(dungeon_session_id)
            JOIN character_session_leases l ON l.character_id=m.character_id AND l.dungeon_session_id=s.dungeon_session_id
            JOIN characters ch ON ch.character_id=m.character_id AND ch.steam_id=m.steam_id
            JOIN rosters r ON r.roster_id=ch.roster_id AND r.steam_id=ch.steam_id
            WHERE s.dungeon_session_id=@session AND s.server_id=@server AND s.state IN ('Loading','InProgress')
              AND s.expires_at>clock_timestamp() AND m.character_id=@character AND m.lease_expires_at>clock_timestamp() AND l.expires_at>clock_timestamp()
              AND m.connection_generation=@generation AND m.confirmed_connection_generation=@generation
              AND m.disconnected_at IS NULL AND m.disconnected_generation=0
              AND NOT EXISTS(SELECT 1 FROM ops_account_restrictions a WHERE a.account_id=ch.steam_id AND a.state='Active' AND a.expires_at>clock_timestamp())
              AND NOT EXISTS(SELECT 1 FROM dungeon_session_abandonments a WHERE a.dungeon_session_id=s.dungeon_session_id AND a.character_id=m.character_id)
              AND NOT EXISTS(SELECT 1 FROM dungeon_session_disconnection_expulsions e WHERE e.dungeon_session_id=s.dungeon_session_id AND e.character_id=m.character_id);
            """, c, tx))
        {
            query.Parameters.AddWithValue("session", dungeonSessionId); query.Parameters.AddWithValue("server", serverId);
            query.Parameters.AddWithValue("character", characterId); query.Parameters.AddWithValue("generation", connectionGeneration);
            await using var reader = await query.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken)) return null;
            roster = reader.GetGuid(0); observed = reader.GetFieldValue<DateTimeOffset>(1); deadline = reader.GetFieldValue<DateTimeOffset>(2);
            if (deadline <= observed) return null;
        }
        var collection = await ReadRidingCollectionAsync(c, tx, roster, cancellationToken);
        var preferences = await ReadRidingPreferencesAsync(c, tx, characterId, cancellationToken);
        await tx.CommitAsync(cancellationToken);
        return new(dungeonSessionId, characterId, roster, connectionGeneration, observed, deadline, new(collection, preferences));
    }
}
