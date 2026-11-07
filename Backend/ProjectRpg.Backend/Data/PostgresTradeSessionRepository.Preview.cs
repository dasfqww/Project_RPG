using System.Data;
using Npgsql;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Domain.Trading.TradeSessionRules;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresTradeSessionRepository
{
    public async Task<TradePreview?> PreviewAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct);
        await using (var readOnly = new NpgsqlCommand("SET TRANSACTION READ ONLY", c, tx)) await readOnly.ExecuteNonQueryAsync(ct);
        var now = await Now(c, tx, ct);
        var session = await Load(c, tx, tradeId, ct);
        if (session is null || !Participant(session.State, actor)) return null;
        Dictionary<Guid, ItemRecord> records = [];
        var ids = session.State.InitiatorOffer.Items.Concat(session.State.InviteeOffer.Items).Select(i => i.ItemId).Distinct().ToArray();
        if (TradePreview.CanInspect(session, now) && ids.Length > 0)
        {
            await using var q = new NpgsqlCommand($"SELECT {PostgresItemRepository.ItemColumns} FROM item_records WHERE item_id=ANY(@ids)", c, tx);
            q.Parameters.AddWithValue("ids", ids);
            await using var r = await q.ExecuteReaderAsync(ct);
            while (await r.ReadAsync(ct)) { var item = PostgresItemRepository.ReadItemRecord(r); records.Add(item.ItemId, item); }
        }
        // The session and both sides' records come from one MVCC snapshot; no locks/reservations survive this read.
        return TradePreview.Create(session, records, policy, now);
    }
}
