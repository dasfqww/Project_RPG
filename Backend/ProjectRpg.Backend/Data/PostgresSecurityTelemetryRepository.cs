using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class PostgresSecurityTelemetryRepository(NpgsqlDataSource dataSource)
    : ISecurityTelemetryRepository
{
    public async Task<SecurityEventStoreResult> StoreBatchAsync(
        IReadOnlyList<SecurityTelemetryEvent> events,
        CancellationToken cancellationToken)
    {
        if (events.Count == 0)
        {
            return new SecurityEventStoreResult(0, 0, []);
        }

        const string insertSql = """
            WITH input (
                event_id, dungeon_session_id, character_id, steam_id,
                server_id, event_type, severity, score, risk_after,
                server_time_seconds, detail, command_fingerprint, received_at
            ) AS (
                SELECT * FROM unnest(
                    @event_ids, @dungeon_session_ids, @character_ids,
                    @steam_ids, @server_ids, @event_types, @severities,
                    @scores, @risk_afters, @server_times, @details,
                    @fingerprints, @received_ats)
            )
            INSERT INTO security_events (
                event_id, dungeon_session_id, character_id, steam_id,
                server_id, event_type, severity, score, risk_after,
                server_time_seconds, detail, command_fingerprint, received_at)
            SELECT
                event_id, dungeon_session_id, character_id, steam_id,
                server_id, event_type, severity, score, risk_after,
                server_time_seconds, detail, command_fingerprint, received_at
            FROM input
            ON CONFLICT (event_id) DO NOTHING
            RETURNING event_id;
            """;
        const string fingerprintSql = """
            SELECT event_id, command_fingerprint
            FROM security_events
            WHERE event_id = ANY(@event_ids);
            """;

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        HashSet<Guid> insertedIds = [];
        await using (NpgsqlCommand insert = new(
            insertSql,
            connection,
            transaction))
        {
            AddArrayParameter(
                insert,
                "event_ids",
                NpgsqlDbType.Uuid,
                events.Select(value => value.EventId).ToArray());
            AddArrayParameter(
                insert,
                "dungeon_session_ids",
                NpgsqlDbType.Uuid,
                events.Select(value => value.DungeonSessionId).ToArray());
            AddArrayParameter(
                insert,
                "character_ids",
                NpgsqlDbType.Uuid,
                events.Select(value => value.CharacterId).ToArray());
            AddArrayParameter(
                insert,
                "steam_ids",
                NpgsqlDbType.Text,
                events.Select(value => value.SteamId).ToArray());
            AddArrayParameter(
                insert,
                "server_ids",
                NpgsqlDbType.Text,
                events.Select(value => value.ServerId).ToArray());
            AddArrayParameter(
                insert,
                "event_types",
                NpgsqlDbType.Text,
                events.Select(value => value.Type.ToString()).ToArray());
            AddArrayParameter(
                insert,
                "severities",
                NpgsqlDbType.Text,
                events.Select(value => value.Severity.ToString()).ToArray());
            AddArrayParameter(
                insert,
                "scores",
                NpgsqlDbType.Double,
                events.Select(value => value.Score).ToArray());
            AddArrayParameter(
                insert,
                "risk_afters",
                NpgsqlDbType.Double,
                events.Select(value => value.RiskAfter).ToArray());
            AddArrayParameter(
                insert,
                "server_times",
                NpgsqlDbType.Double,
                events.Select(value => value.ServerTimeSeconds).ToArray());
            AddArrayParameter(
                insert,
                "details",
                NpgsqlDbType.Text,
                events.Select(value => value.Detail).ToArray());
            AddArrayParameter(
                insert,
                "fingerprints",
                NpgsqlDbType.Text,
                events.Select(value => value.Fingerprint).ToArray());
            AddArrayParameter(
                insert,
                "received_ats",
                NpgsqlDbType.TimestampTz,
                events.Select(value => value.ReceivedAt).ToArray());
            await using NpgsqlDataReader insertedReader =
                await insert.ExecuteReaderAsync(cancellationToken);
            while (await insertedReader.ReadAsync(cancellationToken))
            {
                insertedIds.Add(insertedReader.GetGuid(0));
            }
        }

        Dictionary<Guid, string> storedFingerprints = [];
        await using (NpgsqlCommand readFingerprints = new(
            fingerprintSql,
            connection,
            transaction))
        {
            AddArrayParameter(
                readFingerprints,
                "event_ids",
                NpgsqlDbType.Uuid,
                events.Select(value => value.EventId).ToArray());
            await using NpgsqlDataReader fingerprintReader =
                await readFingerprints.ExecuteReaderAsync(cancellationToken);
            while (await fingerprintReader.ReadAsync(cancellationToken))
            {
                storedFingerprints.Add(
                    fingerprintReader.GetGuid(0),
                    fingerprintReader.GetString(1));
            }
        }

        Guid[] conflicts = events
            .Where(value =>
                !storedFingerprints.TryGetValue(
                    value.EventId,
                    out string? storedFingerprint)
                || !string.Equals(
                    storedFingerprint,
                    value.Fingerprint,
                    StringComparison.Ordinal))
            .Select(value => value.EventId)
            .ToArray();

        if (conflicts.Length > 0)
        {
            await transaction.RollbackAsync(cancellationToken);
            return new SecurityEventStoreResult(0, 0, conflicts);
        }

        await transaction.CommitAsync(cancellationToken);
        return new SecurityEventStoreResult(
            insertedIds.Count,
            events.Count - insertedIds.Count,
            []);
    }

    private static void AddArrayParameter<T>(
        NpgsqlCommand command,
        string name,
        NpgsqlDbType elementType,
        T[] values)
    {
        command.Parameters.AddWithValue(
            name,
            NpgsqlDbType.Array | elementType,
            values);
    }

    public async Task<SecuritySessionSummary> GetSessionSummaryAsync(
        Guid dungeonSessionId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT
                COUNT(*)::BIGINT,
                COALESCE(SUM(score), 0.0)::DOUBLE PRECISION,
                COALESCE(MAX(risk_after), 0.0)::DOUBLE PRECISION,
                MIN(received_at),
                MAX(received_at)
            FROM security_events
            WHERE dungeon_session_id = @dungeon_session_id;

            SELECT event_type, COUNT(*)::BIGINT,
                   SUM(score)::DOUBLE PRECISION
            FROM security_events
            WHERE dungeon_session_id = @dungeon_session_id
            GROUP BY event_type
            ORDER BY event_type;

            SELECT severity, COUNT(*)::BIGINT,
                   SUM(score)::DOUBLE PRECISION
            FROM security_events
            WHERE dungeon_session_id = @dungeon_session_id
            GROUP BY severity
            ORDER BY severity;

            SELECT character_id, COUNT(*)::BIGINT,
                   SUM(score)::DOUBLE PRECISION,
                   MAX(risk_after)::DOUBLE PRECISION
            FROM security_events
            WHERE dungeon_session_id = @dungeon_session_id
            GROUP BY character_id
            ORDER BY character_id;
            """;

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("dungeon_session_id", dungeonSessionId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);

        await reader.ReadAsync(cancellationToken);
        long totalEvents = reader.GetInt64(0);
        double totalScore = reader.GetDouble(1);
        double maximumRiskAfter = reader.GetDouble(2);
        DateTimeOffset? firstReceivedAt = reader.IsDBNull(3)
            ? null
            : reader.GetFieldValue<DateTimeOffset>(3);
        DateTimeOffset? lastReceivedAt = reader.IsDBNull(4)
            ? null
            : reader.GetFieldValue<DateTimeOffset>(4);

        await reader.NextResultAsync(cancellationToken);
        List<SecurityTelemetryBreakdown> byType = [];
        while (await reader.ReadAsync(cancellationToken))
        {
            byType.Add(new SecurityTelemetryBreakdown(
                reader.GetString(0),
                reader.GetInt64(1),
                reader.GetDouble(2)));
        }

        await reader.NextResultAsync(cancellationToken);
        List<SecurityTelemetryBreakdown> bySeverity = [];
        while (await reader.ReadAsync(cancellationToken))
        {
            bySeverity.Add(new SecurityTelemetryBreakdown(
                reader.GetString(0),
                reader.GetInt64(1),
                reader.GetDouble(2)));
        }

        await reader.NextResultAsync(cancellationToken);
        List<SecurityCharacterBreakdown> byCharacter = [];
        while (await reader.ReadAsync(cancellationToken))
        {
            byCharacter.Add(new SecurityCharacterBreakdown(
                reader.GetGuid(0),
                reader.GetInt64(1),
                reader.GetDouble(2),
                reader.GetDouble(3)));
        }

        return new SecuritySessionSummary(
            dungeonSessionId,
            totalEvents,
            totalScore,
            maximumRiskAfter,
            firstReceivedAt,
            lastReceivedAt,
            byType,
            bySeverity,
            byCharacter);
    }
}
