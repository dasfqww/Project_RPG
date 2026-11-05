using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryTradeReservationRepository : ITradeReservationRepository
{
    private readonly InMemoryTransactionGate gate;
    private readonly InMemoryItemRepository items;
    private readonly InMemoryEconomyRepository economy;
    private readonly ICharacterEconomyRepository characters;
    private readonly TradeItemPolicy policy;

    public InMemoryTradeReservationRepository(InMemoryTransactionGate gate, InMemoryItemRepository items,
        InMemoryEconomyRepository economy, ICharacterEconomyRepository characters, TradeItemPolicy policy)
    {
        if (!ReferenceEquals(gate.SyncRoot, items.TransactionSyncRoot)
            || !ReferenceEquals(gate.SyncRoot, economy.TransactionSyncRoot))
            throw new InvalidOperationException("Trade reservations require the same item/currency transaction gate.");
        this.gate = gate; this.items = items; this.economy = economy; this.characters = characters; this.policy = policy;
    }

    public async Task<TradeAssetReservation> ReserveAsync(TradeReservationCommand command, CancellationToken ct)
    {
        command = TradeReservationRules.Validate(command);
        CharacterEconomyContext context = await Context(command.Participant, ct);
        string fingerprint = TradeReservationRules.Fingerprint(command);
        lock (gate.SyncRoot)
        {
            ct.ThrowIfCancellationRequested();
            if (gate.TradeReservations.TryGetValue(command.ReservationId, out var existing))
                return TradeReservationRules.Replay(existing, fingerprint, gate.UtcNow);
            if (command.ExpiresAt <= gate.UtcNow) throw new TradeRuleException("reservation_expired");
            if (gate.TradeReservations.Values.Any(value => value.Command.Participant.CharacterId == command.Participant.CharacterId
                && value.At(gate.UtcNow).Status == TradeReservationStatus.Active))
                throw new TradeRuleException("character_already_reserved");
            var wallet = economy.LoadWalletAsync(context, ct).GetAwaiter().GetResult();
            var balance = wallet.Balances.SingleOrDefault(value => value.Definition.CurrencyCode == command.CurrencyCode)
                ?? throw new TradeRuleException("reservation_currency_unavailable");
            if (command.Offer.CurrencyAmount > balance.Balance - gate.ReservedCurrency(balance.Owner, command.CurrencyCode))
                throw new TradeRuleException("reservation_insufficient_balance");
            foreach (var offered in command.Offer.Items)
            {
                var item = items.FindAsync(offered.ItemId, ct).GetAwaiter().GetResult();
                if (item is null || item.Owner != new ItemOwnerRef(ItemOwnerType.Character, command.Participant.CharacterId.ToString("D")))
                    throw new TradeRuleException("reservation_item_not_found");
                policy.ValidateOfferItem(item, command.Participant, offered, TradeChannel.Direct, gate.UtcNow);
                if (gate.IsItemReserved(item.ItemId)) throw new TradeRuleException("reservation_item_busy");
            }
            if (command.ExpiresAt <= gate.UtcNow) throw new TradeRuleException("reservation_expired");
            var result = new TradeAssetReservation(command, fingerprint, balance.Owner, TradeReservationStatus.Active);
            gate.TradeReservations.Add(command.ReservationId, result);
            return result;
        }
    }

    public async Task<TradeAssetReservation> ReleaseAsync(TradeReservationCommand command, CancellationToken ct)
    {
        command = TradeReservationRules.Validate(command);
        await Context(command.Participant, ct);
        string fingerprint = TradeReservationRules.Fingerprint(command);
        lock (gate.SyncRoot)
        {
            ct.ThrowIfCancellationRequested();
            TradeAssetReservation result;
            if (gate.TradeReservations.TryGetValue(command.ReservationId, out var existing))
                result = TradeReservationRules.Replay(existing, fingerprint, gate.UtcNow) with { Status = TradeReservationStatus.Released };
            else result = new(command, fingerprint, null, TradeReservationStatus.Released);
            gate.TradeReservations[command.ReservationId] = result;
            return result;
        }
    }

    public Task<TradeAssetReservation?> FindAsync(Guid id, TradeParticipant participant, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (gate.SyncRoot)
            return Task.FromResult(gate.TradeReservations.TryGetValue(id, out var value) && value.Command.Participant == participant
                ? value.At(gate.UtcNow) : null);
    }

    private async Task<CharacterEconomyContext> Context(TradeParticipant participant, CancellationToken ct)
    {
        var context = await characters.GetCharacterEconomyContextAsync(participant.CharacterId, ct);
        if (context is null || context.SteamId != participant.AccountId) throw new TradeRuleException("character_not_owned");
        return context;
    }
}
