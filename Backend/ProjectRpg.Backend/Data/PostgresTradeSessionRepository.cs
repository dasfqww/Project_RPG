using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresTradeSessionRepository(NpgsqlDataSource dataSource, TradeItemPolicy policy) : ITradeSessionRepository
{
    private readonly PostgresTradeReservationRepository reservations = new(dataSource, policy);

    public async Task<TradeStoredSession?> FindAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        var session = await Load(c, null, tradeId, ct);
        return session is not null && Participant(session.State, actor) ? session : null;
    }

    public async Task<TradeSessionResult> ExecuteAsync(TradeSessionCommand command, CancellationToken ct)
    {
        Validate(command);
        string fingerprint = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(command))));
        await using var c = await dataSource.OpenConnectionAsync(ct);
        // Participants are immutable. The authoritative state is reread after acquiring all gates.
        var before = await Load(c, null, command.TradeId, ct);
        if (before is not null && !Participant(before.State, command.Actor)) throw new TradeRuleException("trade_participant_required");
        if (before is null && command.Kind != TradeCommandKind.Invite) throw new TradeRuleException("trade_not_found");
        var people = before is null ? new[] { command.Actor, command.Invitee! } : new[] { before.State.Initiator, before.State.Invitee };
        await using var tx = await c.BeginTransactionAsync(ct);
        // Admission takes service(shared) -> account -> game rows. Cancellation/replay must also work during Drain/restriction.
        await PostgresServiceAccess.LockAsync(c, tx, false, ct);
        foreach (var account in people.Select(p => p.AccountId).Distinct().Order(StringComparer.Ordinal))
            await PostgresAccountAccess.LockAsync(c, tx, account, ct);
        await Gate(c, tx, "request", command.RequestId, ct);
        var replay = await Receipt(c, tx, command, fingerprint, ct);
        if (replay is not null) return replay;
        await Gate(c, tx, "session", command.TradeId, ct);
        var session = await Load(c, tx, command.TradeId, ct);
        DateTimeOffset now = await Now(c, tx, ct);
        if (session is null)
        {
            var state = TradeNegotiation.Invite(command.TradeId, command.Actor, command.Invitee!, command.CurrencyCode!, now, now.AddMinutes(5));
            foreach (var p in people.OrderBy(p => p.CharacterId)) await Context(c, tx, p, ct);
            await Access(c, tx, people, ct);
            await using var busy = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM trade_sessions WHERE NOT closed AND expires_at>clock_timestamp() AND (initiator_character=ANY(@ids) OR invitee_character=ANY(@ids)))", c, tx);
            busy.Parameters.AddWithValue("ids", people.Select(p => p.CharacterId).ToArray());
            if ((bool)(await busy.ExecuteScalarAsync(ct))!) throw new TradeRuleException("character_already_trading");
            var definition = await PostgresEconomyRepository.ReadDefinitionAsync(c, tx, state.CurrencyCode, ct);
            if (definition is null || !definition.Enabled) throw new TradeRuleException("reservation_currency_unavailable");
            session = new(state, null, null);
            return await Save(c, tx, command, fingerprint, session, null, now, true, ct);
        }
        if (!Participant(session.State, command.Actor)) throw new TradeRuleException("trade_participant_required");
        if (command.Kind == TradeCommandKind.Invite) throw new TradeRuleException("trade_already_exists");
        if (session.State.Revision != command.ExpectedRevision) throw new TradeRuleException("trade_revision_conflict");
        if (session.Completed || session.State.Status is TradeNegotiationStatus.Cancelled or TradeNegotiationStatus.Expired)
            throw new TradeRuleException("trade_closed");
        foreach (var p in people.OrderBy(p => p.CharacterId)) await Context(c, tx, p, ct);
        Guid nextHold = new(SHA256.HashData(Encoding.UTF8.GetBytes("trade_ready|" + command.RequestId.ToString("D"))).AsSpan(0,16));
        foreach (var id in new[] { session.InitiatorReservation, session.InviteeReservation, command.Kind == TradeCommandKind.Ready ? nextHold : (Guid?)null }
            .Where(id => id.HasValue).Select(id => id!.Value).Distinct().Order()) await LockReservation(c, tx, id, ct);
        await PostgresItemRepository.AcquireOwnerLocksAsync(c, tx, people.Select(p => new ItemOwnerRef(ItemOwnerType.Character, p.CharacterId.ToString("D"))), ct);
        now = await Now(c, tx, ct);
        if (now >= session.State.ExpiresAt || command.Kind == TradeCommandKind.Expire)
        {
            var expired = session.State.Expire(command.ExpectedRevision, now);
            await Release(c, tx, session, ct);
            return await Save(c, tx, command, fingerprint, new(expired, null, null), "trade_expired", now, false, ct);
        }
        string? rejection = null;
        TradeNegotiation stateBefore = session.State;
        switch (command.Kind)
        {
            case TradeCommandKind.Accept:
                await Access(c, tx, people, ct);
                session = session with { State = stateBefore.Accept(command.Actor, command.ExpectedRevision, await Now(c, tx, ct)) };
                break;
            case TradeCommandKind.ReplaceOffer:
                var changed = stateBefore.ReplaceOffer(command.Actor, command.ExpectedRevision, command.Offer!, now);
                await Release(c, tx, session, ct);
                session = new(changed, null, null);
                break;
            case TradeCommandKind.Withdraw:
                var withdrawn = stateBefore.WithdrawReady(command.Actor, command.ExpectedRevision, now);
                await Release(c, tx, session, ct);
                session = new(withdrawn, null, null);
                break;
            case TradeCommandKind.Cancel:
                await Release(c, tx, session, ct);
                session = new(stateBefore.Cancel(command.Actor, command.ExpectedRevision, now), null, null);
                break;
            case TradeCommandKind.Ready:
            case TradeCommandKind.Confirm:
                // Validate command CAS/consent first; stale commands must not withdraw someone else's newer consent.
                var next = command.Kind == TradeCommandKind.Ready
                    ? stateBefore.Ready(command.Actor, command.ExpectedRevision, command.ExpectedOfferRevision, now)
                    : stateBefore.Confirm(command.Actor, command.ExpectedRevision, command.ExpectedOfferRevision, now);
                var original = session;
                await tx.SaveAsync("trade_action", ct);
                try
                {
                    await Access(c, tx, people, ct);
                    if (stateBefore.ExpiresAt <= await Now(c, tx, ct)) throw new TradeRuleException("trade_expired");
                    if (command.Kind == TradeCommandKind.Ready && next.Revision != stateBefore.Revision)
                    {
                        bool first = command.Actor == stateBefore.Initiator;
                        var hold = new TradeReservationCommand(nextHold, stateBefore.TradeId, stateBefore.OfferRevision, command.Actor,
                            stateBefore.CurrencyCode, first ? stateBefore.InitiatorOffer : stateBefore.InviteeOffer, stateBefore.ExpiresAt);
                        var reserved = await reservations.ReserveInTransactionAsync(c, tx, hold, ct);
                        if (reserved.Status != TradeReservationStatus.Active) throw new TradeRuleException("invalid_trade_reservation");
                        session = session with { InitiatorReservation = first ? nextHold : session.InitiatorReservation,
                            InviteeReservation = first ? session.InviteeReservation : nextHold };
                    }
                    if (next.Status == TradeNegotiationStatus.Agreed)
                    {
                        var plan = await Exchange(c, tx, session with { State = next }, ct);
                        // No independently committed Release: all writes and final receipt still share this transaction.
                        await Release(c, tx, session, ct);
                        session = new(next, null, null, true, plan);
                    }
                    else session = session with { State = next };
                }
                catch (Exception e) when (e is TradeRuleException or AccountRestrictedException or ServiceDrainingException)
                {
                    await tx.RollbackAsync("trade_action", ct);
                    session = original;
                    rejection = e is TradeRuleException rule ? rule.Code : e is AccountRestrictedException ? "account_restricted" : "service_draining";
                    await Release(c, tx, session, ct);
                    now = await Now(c, tx, ct);
                    session = new(now >= stateBefore.ExpiresAt ? stateBefore.Expire(stateBefore.Revision, now)
                        : stateBefore.WithdrawReady(command.Actor, stateBefore.Revision, now), null, null);
                }
                break;
            default: throw new TradeRuleException("invalid_trade_command");
        }
        return await Save(c, tx, command, fingerprint, session, rejection, await Now(c, tx, ct), false, ct);
    }

    private async Task Release(NpgsqlConnection c, NpgsqlTransaction tx, TradeStoredSession session, CancellationToken ct)
    {
        foreach (var id in new[] { session.InitiatorReservation, session.InviteeReservation }.Where(id => id.HasValue))
        {
            var hold = await Read(c, tx, id!.Value, ct) ?? throw new InvalidDataException("Session reservation is missing");
            await reservations.ReleaseInTransactionAsync(c, tx, hold.Command, ct);
        }
    }

    private static async Task Access(NpgsqlConnection c, NpgsqlTransaction tx, TradeParticipant[] people, CancellationToken ct)
    {
        await PostgresServiceAccess.EnsureOpenAsync(c, tx, ct);
        foreach (var p in people.OrderBy(p => p.AccountId, StringComparer.Ordinal))
            await PostgresAccountAccess.EnsureAsync(c, tx, p.AccountId, await Now(c, tx, ct), ct);
        await using var query = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM character_session_leases WHERE character_id=ANY(@ids) AND expires_at>clock_timestamp())", c, tx);
        query.Parameters.AddWithValue("ids", people.Select(p => p.CharacterId).ToArray());
        if ((bool)(await query.ExecuteScalarAsync(ct))!) throw new TradeRuleException("trade_requires_lobby");
    }

    private static bool Participant(TradeNegotiation s, TradeParticipant p) => p == s.Initiator || p == s.Invitee;
    private static void Validate(TradeSessionCommand c)
    {
        if (c is null || c.RequestId == Guid.Empty || c.TradeId == Guid.Empty || c.Actor is null || c.Actor.CharacterId == Guid.Empty
            || string.IsNullOrWhiteSpace(c.Actor.AccountId) || c.Actor.AccountId.Length > 20 || !Enum.IsDefined(c.Kind)
            || (c.Kind == TradeCommandKind.Invite ? c.Invitee is null || string.IsNullOrWhiteSpace(c.CurrencyCode)
                || c.ExpectedRevision != 0 || c.ExpectedOfferRevision != 0 : c.Invitee is not null || c.CurrencyCode is not null)
            || (c.Kind == TradeCommandKind.ReplaceOffer ? c.Offer is null : c.Offer is not null))
            throw new TradeRuleException("invalid_trade_command");
    }
    private static async Task Gate(NpgsqlConnection c, NpgsqlTransaction tx, string kind, Guid id, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", c, tx);
        q.Parameters.AddWithValue("key", "trade_" + kind + ":" + id.ToString("D")); await q.ExecuteNonQueryAsync(ct);
    }
    private static async Task<TradeStoredSession?> Load(NpgsqlConnection c, NpgsqlTransaction? tx, Guid id, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT payload::text FROM trade_sessions WHERE trade_id=@id", c, tx);
        q.Parameters.AddWithValue("id", id);
        return await q.ExecuteScalarAsync(ct) is string json ? JsonSerializer.Deserialize<TradeStoredSession>(json)
            ?? throw new InvalidDataException("Invalid trade session") : null;
    }
    private static async Task<TradeSessionResult?> Receipt(NpgsqlConnection c, NpgsqlTransaction tx, TradeSessionCommand cmd, string fingerprint, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT fingerprint, result::text FROM trade_session_receipts WHERE request_id=@id", c, tx);
        q.Parameters.AddWithValue("id", cmd.RequestId);
        await using var r = await q.ExecuteReaderAsync(ct);
        if (!await r.ReadAsync(ct)) return null;
        if (r.GetString(0) != fingerprint) throw new TradeRuleException("trade_idempotency_conflict");
        return JsonSerializer.Deserialize<TradeSessionResult>(r.GetString(1)) ?? throw new InvalidDataException("Invalid trade receipt");
    }
    private static async Task<TradeSessionResult> Save(NpgsqlConnection c, NpgsqlTransaction tx, TradeSessionCommand cmd,
        string fingerprint, TradeStoredSession session, string? rejection, DateTimeOffset now, bool insert, CancellationToken ct)
    {
        var s = session.State;
        await using var q = new NpgsqlCommand(insert ? """
            INSERT INTO trade_sessions(trade_id,initiator_character,invitee_character,initiator_account,invitee_account,expires_at,closed,revision,payload)
            VALUES(@id,@a,@b,@aa,@ba,@expires,@closed,@revision,@payload)
            """ : "UPDATE trade_sessions SET closed=@closed,revision=@revision,payload=@payload WHERE trade_id=@id AND revision=@expected", c, tx);
        q.Parameters.AddWithValue("id", s.TradeId); q.Parameters.AddWithValue("a", s.Initiator.CharacterId); q.Parameters.AddWithValue("b", s.Invitee.CharacterId);
        q.Parameters.AddWithValue("aa", s.Initiator.AccountId); q.Parameters.AddWithValue("ba", s.Invitee.AccountId);
        q.Parameters.AddWithValue("expires", s.ExpiresAt); q.Parameters.AddWithValue("closed", session.Completed || s.Status is TradeNegotiationStatus.Cancelled or TradeNegotiationStatus.Expired);
        q.Parameters.AddWithValue("revision", s.Revision); q.Parameters.AddWithValue("expected", cmd.ExpectedRevision);
        q.Parameters.AddWithValue("payload", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(session));
        if (await q.ExecuteNonQueryAsync(ct) != 1) throw new InvalidDataException("Trade CAS changed under lock");
        TradeSessionResult result = new(cmd.RequestId, session, rejection, now);
        await using var receipt = new NpgsqlCommand("INSERT INTO trade_session_receipts VALUES(@request,@trade,@account,@character,@fingerprint,@result,@now)", c, tx);
        receipt.Parameters.AddWithValue("request", cmd.RequestId); receipt.Parameters.AddWithValue("trade", cmd.TradeId);
        receipt.Parameters.AddWithValue("account", cmd.Actor.AccountId); receipt.Parameters.AddWithValue("character", cmd.Actor.CharacterId);
        receipt.Parameters.AddWithValue("fingerprint", fingerprint); receipt.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(result));
        receipt.Parameters.AddWithValue("now", now); await receipt.ExecuteNonQueryAsync(ct);
        await tx.CommitAsync(ct); return result;
    }
}
