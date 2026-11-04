using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Sessions;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

public sealed class PostgresSessionEvidenceReadStore(NpgsqlDataSource source, TimeProvider clock, OperationsStorageTrace trace) : ISessionEvidenceReadStore
{
    public Task<Result<Page<SessionEvidence>>> ReadAsync(Guid characterId, PageRequest page, CancellationToken ct) =>
        PostgresReadSession.RunAsync(source, trace, async session =>
        {
            if (!await session.CharacterExistsAsync(characterId, ct)) return Result<Page<SessionEvidence>>.Fail(Failures.CharacterMissing);
            DateTimeOffset now = clock.GetUtcNow();
            await using NpgsqlCommand command = session.Command("""
                SELECT s.dungeon_session_id,s.dungeon_id,s.difficulty,s.state,s.updated_at,s.expires_at,
                    m.lease_expires_at,l.expires_at,r.state,r.attempt_count,r.next_attempt_at,r.completed_at,
                    (r.last_error IS NOT NULL AND r.last_error <> '')
                FROM dungeon_session_members m JOIN dungeon_sessions s ON s.dungeon_session_id=m.dungeon_session_id
                LEFT JOIN character_session_leases l ON l.character_id=m.character_id AND l.dungeon_session_id=s.dungeon_session_id
                LEFT JOIN dungeon_reward_settlements r ON r.dungeon_session_id=s.dungeon_session_id
                WHERE m.character_id=@id AND (@after_id IS NULL OR (s.updated_at,s.dungeon_session_id)<(@after_time,@after_id))
                ORDER BY s.updated_at DESC,s.dungeon_session_id DESC LIMIT @limit;
                """);
            command.Parameters.AddWithValue("id", characterId);
            command.Parameters.AddWithValue("after_id", NpgsqlDbType.Uuid, (object?)page.After?.Id ?? DBNull.Value);
            command.Parameters.AddWithValue("after_time", NpgsqlDbType.TimestampTz, (object?)page.After?.Timestamp ?? DBNull.Value);
            command.Parameters.AddWithValue("limit", page.Limit + 1);
            List<SessionEvidence> rows = [];
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                DateTimeOffset expiry = reader.GetFieldValue<DateTimeOffset>(5);
                DateTimeOffset? member = reader.IsDBNull(6) ? null : reader.GetFieldValue<DateTimeOffset>(6);
                DateTimeOffset? lease = reader.IsDBNull(7) ? null : reader.GetFieldValue<DateTimeOffset>(7);
                SettlementSummary? settlement = reader.IsDBNull(8) ? null : new(reader.GetString(8), reader.GetInt32(9),
                    reader.GetFieldValue<DateTimeOffset>(10), reader.IsDBNull(11) ? null : reader.GetFieldValue<DateTimeOffset>(11),
                    reader.IsDBNull(12) || !reader.GetBoolean(12) ? null : "settlement_error");
                rows.Add(new(reader.GetGuid(0), reader.GetString(1), reader.GetString(2), reader.GetString(3),
                    reader.GetFieldValue<DateTimeOffset>(4), expiry, member, lease, SessionEvidence.Observe(now, expiry, member, lease), settlement));
            }
            return Result<Page<SessionEvidence>>.Success(ReadMapping.Page(rows, page.Limit, s => new(s.DungeonSessionId, s.UpdatedAtUtc)));
        }, ct);
}
