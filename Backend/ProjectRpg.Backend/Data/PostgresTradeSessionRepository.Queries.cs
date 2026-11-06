using System.Text.Json;
using Npgsql;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresTradeSessionRepository
{
    public async Task<TradeStoredSession?> FindActiveAsync(TradeParticipant actor, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var q = new NpgsqlCommand("""
            SELECT payload::text FROM trade_sessions WHERE NOT closed AND expires_at>clock_timestamp()
            AND ((initiator_character=@id AND initiator_account=@account) OR (invitee_character=@id AND invitee_account=@account))
            ORDER BY expires_at,trade_id LIMIT 1
            """, c);
        q.Parameters.AddWithValue("id", actor.CharacterId); q.Parameters.AddWithValue("account", actor.AccountId);
        return await q.ExecuteScalarAsync(ct) is string json ? JsonSerializer.Deserialize<TradeStoredSession>(json)
            ?? throw new InvalidDataException("Invalid trade session") : null;
    }

    public async Task<IReadOnlyList<TradeStoredSession>> FindExpiredAsync(int limit, CancellationToken ct)
    {
        if (limit is < 1 or > 100) throw new ArgumentOutOfRangeException(nameof(limit));
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var q = new NpgsqlCommand("""
            SELECT payload::text FROM trade_sessions WHERE NOT closed AND expires_at<=clock_timestamp()
            ORDER BY expires_at,trade_id LIMIT @limit
            """, c);
        q.Parameters.AddWithValue("limit", limit);
        await using var reader = await q.ExecuteReaderAsync(ct);
        List<TradeStoredSession> result = [];
        while (await reader.ReadAsync(ct)) result.Add(JsonSerializer.Deserialize<TradeStoredSession>(reader.GetString(0))
            ?? throw new InvalidDataException("Invalid trade session"));
        return result;
    }
}
