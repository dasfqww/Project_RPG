using System.Data;
using Npgsql;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Domain.Trading.TradeSessionRules;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresTradeSessionRepository
{
    public async Task<TradeAssets?> AssetsAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct)
    {
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await connection.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct);
        await using (var readOnly = new NpgsqlCommand("SET TRANSACTION READ ONLY", connection, tx)) await readOnly.ExecuteNonQueryAsync(ct);
        var now = await Now(connection, tx, ct);
        var session = await Load(connection, tx, tradeId, ct);
        if (session is null || !Participant(session.State, actor)) return null;
        if (!session.Completed) throw new TradeRuleException("trade_not_completed");
        Guid roster;
        await using (var context = new NpgsqlCommand("SELECT roster_id FROM characters WHERE character_id=@id AND steam_id=@account", connection, tx))
        {
            context.Parameters.AddWithValue("id", actor.CharacterId); context.Parameters.AddWithValue("account", actor.AccountId);
            if (await context.ExecuteScalarAsync(ct) is not Guid id) throw new TradeRuleException("character_not_owned");
            roster = id;
        }
        List<ItemRecord> records = [];
        await using (var query = new NpgsqlCommand($"""
            SELECT {PostgresItemRepository.ItemColumns} FROM item_records
            WHERE owner_type='Character' AND owner_id=@owner AND lifecycle_state='Active'
            ORDER BY container_type,container_id,slot_index,item_id LIMIT @limit
            """, connection, tx))
        {
            query.Parameters.AddWithValue("owner", actor.CharacterId.ToString("D"));
            query.Parameters.AddWithValue("limit", TradeAssets.MaximumItems + 1);
            await using var reader = await query.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct)) records.Add(PostgresItemRepository.ReadItemRecord(reader));
        }
        var wallet = await PostgresEconomyRepository.ReadWalletAsync(connection, tx, new(actor.CharacterId, roster, actor.AccountId), ct);
        return TradeAssets.Create(session, actor, wallet, records, now);
    }
}
