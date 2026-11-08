using Npgsql;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresTradeSessionRepository
{
    public async Task<IReadOnlyList<TradeTarget>> FindTargetsAsync(TradeParticipant actor, string name, CancellationToken ct)
    {
        name = TradeTargetSearch.Name(name);
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await connection.BeginTransactionAsync(ct);
        // Same service -> account -> character order as invitations. This read grants no reservation.
        await Access(connection, tx, [actor], ct);
        var context = await Context(connection, tx, actor, ct);
        await using (var busy = new NpgsqlCommand("""
            SELECT EXISTS(SELECT 1 FROM trade_sessions WHERE NOT closed AND expires_at>clock_timestamp()
                AND (initiator_character=@id OR invitee_character=@id))
            """, connection, tx))
        {
            busy.Parameters.AddWithValue("id", actor.CharacterId);
            if ((bool)(await busy.ExecuteScalarAsync(ct))!) throw new TradeRuleException("character_already_trading");
        }
        await using var query = new NpgsqlCommand("""
            SELECT c.character_id,c.name FROM characters c
            JOIN rosters target ON target.roster_id=c.roster_id
            JOIN rosters own ON own.roster_id=@roster
            WHERE lower(c.name)=lower(@name) AND c.steam_id<>@account AND target.world_id=own.world_id
            AND NOT EXISTS(SELECT 1 FROM ops_account_restrictions r
                WHERE r.account_id=c.steam_id AND r.state='Active' AND r.expires_at>clock_timestamp())
            AND NOT EXISTS(SELECT 1 FROM character_session_leases l
                WHERE l.character_id=c.character_id AND l.expires_at>clock_timestamp())
            AND NOT EXISTS(SELECT 1 FROM trade_sessions t WHERE NOT t.closed AND t.expires_at>clock_timestamp()
                AND (t.initiator_character=c.character_id OR t.invitee_character=c.character_id))
            LIMIT 1
            """, connection, tx);
        query.Parameters.AddWithValue("roster", context.RosterId);
        query.Parameters.AddWithValue("name", name);
        query.Parameters.AddWithValue("account", actor.AccountId);
        await using var reader = await query.ExecuteReaderAsync(ct);
        // No game rows are changed. Disposing the read transaction releases only its shared gates.
        return await reader.ReadAsync(ct) ? [new(reader.GetGuid(0), reader.GetString(1))] : [];
    }
}
