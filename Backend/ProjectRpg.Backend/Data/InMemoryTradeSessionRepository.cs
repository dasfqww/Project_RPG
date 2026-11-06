using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Domain.Trading.TradeSessionRules;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryTradeSessionRepository : ITradeSessionRepository
{
    private readonly InMemoryTransactionGate gate;
    private readonly InMemoryGameRepository game;
    private readonly InMemoryItemRepository items;
    private readonly InMemoryEconomyRepository economy;
    private readonly InMemoryTradeReservationRepository reservations;
    private readonly TradeItemPolicy policy;

    public InMemoryTradeSessionRepository(InMemoryTransactionGate gate, InMemoryGameRepository game,
        InMemoryItemRepository items, InMemoryEconomyRepository economy, TradeItemPolicy policy)
    {
        game.ValidateTradeAssets(gate, items);
        reservations = new(gate, items, economy, game, policy); // verifies the same currency gate too
        this.gate = gate; this.game = game; this.items = items; this.economy = economy; this.policy = policy;
    }

    public Task<TradeStoredSession?> FindAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct) =>
        Task.FromResult(game.WithTradeLocks(gate, () =>
        {
            ct.ThrowIfCancellationRequested();
            var session = Load(tradeId);
            return session is not null && Participant(session.State, actor) ? session : null;
        }));

    public Task<TradeSessionResult> ExecuteAsync(TradeSessionCommand command, CancellationToken ct)
    {
        Validate(command);
        return Task.FromResult(game.WithTradeLocks(gate, () =>
        {
            ct.ThrowIfCancellationRequested();
            var snapshot = Capture();
            try { return Execute(command, ct); }
            catch { Restore(snapshot); throw; }
        }));
    }

    private TradeSessionResult Execute(TradeSessionCommand command, CancellationToken ct)
    {
        var session = Load(command.TradeId);
        if (session is not null && !Participant(session.State, command.Actor)) throw new TradeRuleException("trade_participant_required");
        if (session is null && command.Kind != TradeCommandKind.Invite) throw new TradeRuleException("trade_not_found");
        string fingerprint = Fingerprint(command);
        if (gate.TradeSessionReceipts.TryGetValue(command.RequestId, out var replay))
        {
            if (replay.Fingerprint != fingerprint) throw new TradeRuleException("trade_idempotency_conflict");
            return JsonSerializer.Deserialize<TradeSessionResult>(replay.Result) ?? throw new InvalidDataException("Invalid trade receipt");
        }
        var people = session is null ? new[] { command.Actor, command.Invitee! } : new[] { session.State.Initiator, session.State.Invitee };
        DateTimeOffset now = gate.UtcNow;
        if (session is null)
        {
            var state = TradeNegotiation.Invite(command.TradeId, command.Actor, command.Invitee!, command.CurrencyCode!, now, now.AddMinutes(5));
            foreach (var p in people) Context(p, ct);
            game.EnsureTradeAccess(people, gate.UtcNow);
            if (gate.TradeSessions.Keys.Select(id => Load(id)!).Any(existing => !existing.Completed
                && existing.State.Status is not (TradeNegotiationStatus.Cancelled or TradeNegotiationStatus.Expired)
                && existing.State.ExpiresAt > gate.UtcNow && people.Any(p => p.CharacterId == existing.State.Initiator.CharacterId
                    || p.CharacterId == existing.State.Invitee.CharacterId)))
                throw new TradeRuleException("character_already_trading");
            var definition = economy.GetDefinitionsAsync(ct).GetAwaiter().GetResult().SingleOrDefault(d => d.CurrencyCode == state.CurrencyCode);
            if (definition is null || !definition.Enabled) throw new TradeRuleException("reservation_currency_unavailable");
            return Save(command, fingerprint, new(state, null, null), null, now, ct);
        }
        if (command.Kind == TradeCommandKind.Invite) throw new TradeRuleException("trade_already_exists");
        if (session.State.Revision != command.ExpectedRevision) throw new TradeRuleException("trade_revision_conflict");
        if (session.Completed || session.State.Status is TradeNegotiationStatus.Cancelled or TradeNegotiationStatus.Expired)
            throw new TradeRuleException("trade_closed");
        foreach (var p in people) Context(p, ct);
        now = gate.UtcNow;
        if (now >= session.State.ExpiresAt || command.Kind == TradeCommandKind.Expire)
        {
            var expired = session.State.Expire(command.ExpectedRevision, now);
            Release(session, ct);
            return Save(command, fingerprint, new(expired, null, null), "trade_expired", now, ct);
        }
        var before = session.State;
        string? rejection = null;
        switch (command.Kind)
        {
            case TradeCommandKind.Accept:
                game.EnsureTradeAccess(people, gate.UtcNow);
                session = session with { State = before.Accept(command.Actor, command.ExpectedRevision, gate.UtcNow) };
                break;
            case TradeCommandKind.ReplaceOffer:
                var changed = before.ReplaceOffer(command.Actor, command.ExpectedRevision, command.Offer!, now);
                Release(session, ct); session = new(changed, null, null); break;
            case TradeCommandKind.Withdraw:
                var withdrawn = before.WithdrawReady(command.Actor, command.ExpectedRevision, now);
                Release(session, ct); session = new(withdrawn, null, null); break;
            case TradeCommandKind.Cancel:
                Release(session, ct); session = new(before.Cancel(command.Actor, command.ExpectedRevision, now), null, null); break;
            case TradeCommandKind.Ready:
            case TradeCommandKind.Confirm:
                var next = command.Kind == TradeCommandKind.Ready
                    ? before.Ready(command.Actor, command.ExpectedRevision, command.ExpectedOfferRevision, now)
                    : before.Confirm(command.Actor, command.ExpectedRevision, command.ExpectedOfferRevision, now);
                var original = session;
                var savepoint = Capture();
                try
                {
                    game.EnsureTradeAccess(people, gate.UtcNow);
                    if (before.ExpiresAt <= gate.UtcNow) throw new TradeRuleException("trade_expired");
                    if (command.Kind == TradeCommandKind.Ready && next.Revision != before.Revision)
                    {
                        bool first = command.Actor == before.Initiator;
                        Guid id = ReadyReservationId(command.RequestId);
                        var reserved = reservations.ReserveAsync(new(id, before.TradeId, before.OfferRevision, command.Actor,
                            before.CurrencyCode, first ? before.InitiatorOffer : before.InviteeOffer, before.ExpiresAt), ct).GetAwaiter().GetResult();
                        if (reserved.Status != TradeReservationStatus.Active) throw new TradeRuleException("invalid_trade_reservation");
                        session = session with { InitiatorReservation = first ? id : session.InitiatorReservation,
                            InviteeReservation = first ? session.InviteeReservation : id };
                    }
                    if (next.Status == TradeNegotiationStatus.Agreed)
                    {
                        var plan = Exchange(session with { State = next }, ct);
                        Release(session, ct);
                        session = new(next, null, null, true, plan);
                    }
                    else session = session with { State = next };
                }
                catch (Exception e) when (e is TradeRuleException or AccountRestrictedException or ServiceDrainingException)
                {
                    Restore(savepoint); session = original;
                    rejection = e is TradeRuleException rule ? rule.Code : e is AccountRestrictedException ? "account_restricted" : "service_draining";
                    Release(session, ct); now = gate.UtcNow;
                    session = new(now >= before.ExpiresAt ? before.Expire(before.Revision, now)
                        : before.WithdrawReady(command.Actor, before.Revision, now), null, null);
                }
                break;
            default: throw new TradeRuleException("invalid_trade_command");
        }
        return Save(command, fingerprint, session, rejection, gate.UtcNow, ct);
    }

    private TradeExchangePlan Exchange(TradeStoredSession session, CancellationToken ct)
    {
        var state = session.State;
        var people = new[] { state.Initiator, state.Invitee };
        var holds = new[] { session.InitiatorReservation, session.InviteeReservation }.Select(id => id is { } value
            && gate.TradeReservations.TryGetValue(value, out var hold) ? hold : throw new TradeRuleException("invalid_trade_reservation")).ToArray();
        List<TradeExchangeAssets> inputs = [];
        for (int i = 0; i < 2; i++)
        {
            var context = Context(people[i], ct);
            var balance = economy.LoadWalletAsync(context, ct).GetAwaiter().GetResult().Balances.SingleOrDefault(b => b.Definition.CurrencyCode == state.CurrencyCode)
                ?? throw new TradeRuleException("reservation_currency_unavailable");
            if (holds[i].At(gate.UtcNow).Status != TradeReservationStatus.Active) throw new TradeRuleException("invalid_trade_reservation");
            var inventory = items.TradeInventory(new(ItemOwnerType.Character, people[i].CharacterId.ToString("D")));
            var reserved = inventory.Where(item => gate.IsItemReserved(item.ItemId)).Select(item => item.ItemId).ToHashSet();
            reserved.ExceptWith(holds[i].Command.Offer.Items.Select(item => item.ItemId));
            long amount = gate.ReservedCurrency(balance.Owner, state.CurrencyCode);
            if (holds[i].At(gate.UtcNow).Status != TradeReservationStatus.Active) throw new TradeRuleException("invalid_trade_reservation");
            inputs.Add(new(context, inventory, balance, checked(amount - holds[i].Command.Offer.CurrencyAmount), reserved));
        }
        var plan = TradeExchangePlanner.Plan(state, inputs[0], inputs[1], holds[0], holds[1], policy, gate.UtcNow);
        items.ApplyTradePlan(plan.Items);
        economy.ApplyTradePlan(plan.Currency);
        return plan;
    }

    private CharacterEconomyContext Context(TradeParticipant p, CancellationToken ct)
    {
        var context = game.GetCharacterEconomyContextAsync(p.CharacterId, ct).GetAwaiter().GetResult();
        if (context is null || context.SteamId != p.AccountId) throw new TradeRuleException("character_not_owned");
        return context;
    }
    private void Release(TradeStoredSession session, CancellationToken ct)
    {
        foreach (var id in new[] { session.InitiatorReservation, session.InviteeReservation }.Where(id => id.HasValue))
        {
            if (!gate.TradeReservations.TryGetValue(id!.Value, out var hold)) throw new InvalidDataException("Session reservation is missing");
            reservations.ReleaseAsync(hold.Command, ct).GetAwaiter().GetResult();
        }
    }
    private TradeStoredSession? Load(Guid id) => gate.TradeSessions.TryGetValue(id, out var json)
        ? JsonSerializer.Deserialize<TradeStoredSession>(json) ?? throw new InvalidDataException("Invalid trade session") : null;
    private TradeSessionResult Save(TradeSessionCommand command, string fingerprint, TradeStoredSession session, string? rejection, DateTimeOffset now, CancellationToken ct)
    {
        TradeSessionResult result = new(command.RequestId, session, rejection, now);
        string stateJson = JsonSerializer.Serialize(session), resultJson = JsonSerializer.Serialize(result);
        ct.ThrowIfCancellationRequested();
        gate.TradeSessions[command.TradeId] = stateJson;
        gate.TradeSessionReceipts.Add(command.RequestId, (fingerprint, resultJson));
        return result;
    }
    private sealed record Snapshot(InMemoryItemRepository.Snapshot Items, InMemoryEconomyRepository.Snapshot Economy,
        Dictionary<Guid, TradeAssetReservation> Holds, Dictionary<Guid, string> Sessions,
        Dictionary<Guid, (string Fingerprint, string Result)> Receipts);
    private Snapshot Capture() => new(items.CaptureSnapshot(), economy.CaptureSnapshot(), new(gate.TradeReservations), new(gate.TradeSessions), new(gate.TradeSessionReceipts));
    private void Restore(Snapshot snapshot)
    {
        items.RestoreSnapshot(snapshot.Items); economy.RestoreSnapshot(snapshot.Economy);
        RestoreMap(gate.TradeReservations, snapshot.Holds); RestoreMap(gate.TradeSessions, snapshot.Sessions); RestoreMap(gate.TradeSessionReceipts, snapshot.Receipts);
    }
    private static void RestoreMap<T>(Dictionary<Guid, T> target, Dictionary<Guid, T> snapshot)
    { target.Clear(); foreach (var entry in snapshot) target.Add(entry.Key, entry.Value); }
}
