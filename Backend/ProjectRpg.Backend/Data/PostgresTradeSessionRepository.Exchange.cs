using Npgsql;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresTradeSessionRepository
{
    private async Task<TradeExchangePlan> Exchange(NpgsqlConnection c, NpgsqlTransaction tx, TradeStoredSession session, CancellationToken ct)
    {
        var s = session.State;
        var people = new[] { s.Initiator, s.Invitee };
        var ids = new[] { session.InitiatorReservation, session.InviteeReservation };
        var holds = new List<TradeAssetReservation>();
        foreach (var id in ids)
        {
            var hold = id is { } value ? await Read(c, tx, value, ct) : null;
            if (hold is null || hold.At(await Now(c, tx, ct)).Status != TradeReservationStatus.Active)
                throw new TradeRuleException("invalid_trade_reservation");
            holds.Add(hold);
        }
        var definition = await PostgresEconomyRepository.ReadDefinitionAsync(c, tx, s.CurrencyCode, ct)
            ?? throw new TradeRuleException("reservation_currency_unavailable");
        var contexts = new List<CharacterEconomyContext>();
        foreach (var p in people) contexts.Add(await Context(c, tx, p, ct));
        var owners = contexts.Select(context => new CurrencyOwnerRef(definition.Scope, EconomyRules.ResolveOwnerId(definition.Scope, context))).ToArray();
        Dictionary<CurrencyOwnerRef, CurrencyBalance> balances = [];
        foreach (var owner in owners.Distinct().OrderBy(o => o.Scope).ThenBy(o => o.OwnerId, StringComparer.Ordinal))
        {
            await PostgresEconomyRepository.EnsureBalanceRowAsync(c, tx, owner, s.CurrencyCode, await Now(c, tx, ct), ct);
            var value = await PostgresEconomyRepository.LockBalanceAsync(c, tx, owner, s.CurrencyCode, ct);
            balances.Add(owner, new(definition, owner, value.Balance, value.Revision));
        }
        List<TradeExchangeAssets> inputs = [];
        for (int i = 0; i < 2; i++)
        {
            List<ItemRecord> inventory = [];
            await using (var q = new NpgsqlCommand($"SELECT {PostgresItemRepository.ItemColumns} FROM item_records WHERE owner_type='Character' AND owner_id=@id AND container_type='Inventory' AND lifecycle_state='Active' ORDER BY item_id FOR UPDATE", c, tx))
            {
                q.Parameters.AddWithValue("id", people[i].CharacterId.ToString("D"));
                await using var r = await q.ExecuteReaderAsync(ct);
                while (await r.ReadAsync(ct)) inventory.Add(PostgresItemRepository.ReadItemRecord(r));
            }
            var heldItems = await PostgresTradeReservationGuards.ReservedItems(c, tx, inventory.Select(item => item.ItemId), ct);
            heldItems.ExceptWith(holds[i].Command.Offer.Items.Select(item => item.ItemId));
            long total = await PostgresTradeReservationGuards.ReservedCurrency(c, tx, owners[i], s.CurrencyCode, ct);
            // Recheck after wallet/item lock waits. Never subtract an already expired hold from another trade's funds.
            if (holds[i].At(await Now(c, tx, ct)).Status != TradeReservationStatus.Active)
                throw new TradeRuleException("invalid_trade_reservation");
            inputs.Add(new(contexts[i], inventory, balances[owners[i]], checked(total - holds[i].Command.Offer.CurrencyAmount), heldItems));
        }
        var plan = TradeExchangePlanner.Plan(s, inputs[0], inputs[1], holds[0], holds[1], policy, await Now(c, tx, ct));
        foreach (var mutation in plan.Items)
        {
            var current = await PostgresItemRepository.ReadItemForUpdateAsync(c, tx, mutation.NewRecord.ItemId, ct);
            if (mutation.ExpectedRevision == 0 ? current is not null : current is null || current.Revision != mutation.ExpectedRevision)
                throw new TradeRuleException("trade_item_revision_conflict");
        }
        // Free all outgoing/merge slots inside the transaction before writing final locations; UNIQUE stays enforced.
        foreach (var mutation in plan.Items.Where(m => m.ExpectedRevision > 0))
            await PostgresItemRepository.StageExistingRecordAsync(c, tx, mutation.NewRecord.ItemId, ct);
        foreach (var mutation in plan.Items)
            await PostgresItemRepository.WriteRecordAsync(c, tx,
                ItemRecordRules.SnapshotWithRevision(mutation.NewRecord, checked(mutation.ExpectedRevision + 1)),
                mutation.ExpectedRevision == 0, await Now(c, tx, ct), ct);
        foreach (var movement in plan.Currency)
        {
            await using var q = new NpgsqlCommand("UPDATE currency_balances SET balance=@next,revision=revision+1,updated_at=clock_timestamp() WHERE owner_type=@type AND owner_id=@owner AND currency_code=@code AND revision=@revision AND balance=@previous", c, tx);
            q.Parameters.AddWithValue("next", movement.NewBalance); q.Parameters.AddWithValue("previous", movement.PreviousBalance);
            q.Parameters.AddWithValue("revision", movement.ExpectedRevision); q.Parameters.AddWithValue("type", movement.Owner.Scope.ToString());
            q.Parameters.AddWithValue("owner", movement.Owner.OwnerId); q.Parameters.AddWithValue("code", movement.CurrencyCode);
            if (await q.ExecuteNonQueryAsync(ct) != 1) throw new InvalidDataException("Wallet changed under trade lock");
        }
        return plan;
    }
}
