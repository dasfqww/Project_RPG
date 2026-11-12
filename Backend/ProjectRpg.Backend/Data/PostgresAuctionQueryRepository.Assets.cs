using System.Data;
using Npgsql;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresAuctionQueryRepository
{
    public async Task<AuctionAssets?> AssetsAsync(Guid request, TradeParticipant actor, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct);
        await using (var mode = new NpgsqlCommand("SET TRANSACTION READ ONLY", c, tx)) await mode.ExecuteNonQueryAsync(ct);
        var now = await Now(c, tx, ct);
        if (await ReadReceipt(c, tx, request, actor, ct) is null) return null;
        Guid roster;
        await using (var owner = new NpgsqlCommand("SELECT roster_id FROM characters WHERE character_id=@id AND steam_id=@account", c, tx))
        {
            owner.Parameters.AddWithValue("id", actor.CharacterId); owner.Parameters.AddWithValue("account", actor.AccountId);
            if (await owner.ExecuteScalarAsync(ct) is not Guid id) throw new TradeRuleException("character_not_owned");
            roster = id;
        }
        List<ItemRecord> records = [];
        await using (var q = new NpgsqlCommand($"SELECT {PostgresItemRepository.ItemColumns} FROM item_records WHERE owner_type='Character' AND owner_id=@owner AND lifecycle_state='Active' ORDER BY container_type,container_id,slot_index,item_id LIMIT @limit", c, tx))
        {
            q.Parameters.AddWithValue("owner", actor.CharacterId.ToString("D")); q.Parameters.AddWithValue("limit", AuctionAssets.MaximumItems + 1);
            await using var r = await q.ExecuteReaderAsync(ct); while (await r.ReadAsync(ct)) records.Add(PostgresItemRepository.ReadItemRecord(r));
        }
        var wallet = await PostgresEconomyRepository.ReadWalletAsync(c, tx, new(actor.CharacterId, roster, actor.AccountId), ct);
        return AuctionAssets.Create(request, actor, wallet, records, now);
    }
}
