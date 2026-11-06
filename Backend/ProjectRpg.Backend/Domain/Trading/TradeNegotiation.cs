namespace ProjectRpg.Backend.Domain.Trading;

public enum TradeNegotiationStatus { Invited, Negotiating, Agreed, Cancelled, Expired }

/// <summary>
/// Pure immutable consent state. The application must authenticate participants; the repository
/// must atomically CAS Revision with reservations and settlement. Agreed never means assets moved.
/// </summary>
public sealed class TradeNegotiation
{
    public Guid TradeId { get; }
    public TradeParticipant Initiator { get; }
    public TradeParticipant Invitee { get; }
    public string CurrencyCode { get; }
    public DateTimeOffset ExpiresAt { get; }
    public long Revision { get; }
    public long OfferRevision { get; }
    public TradeNegotiationStatus Status { get; }
    public TradeOffer InitiatorOffer { get; }
    public TradeOffer InviteeOffer { get; }
    public bool InitiatorReady { get; }
    public bool InviteeReady { get; }
    public bool InitiatorConfirmed { get; }
    public bool InviteeConfirmed { get; }

    [System.Text.Json.Serialization.JsonConstructor]
    private TradeNegotiation(Guid tradeId, TradeParticipant initiator, TradeParticipant invitee,
        string currencyCode, DateTimeOffset expiresAt, long revision, long offerRevision,
        TradeNegotiationStatus status, TradeOffer initiatorOffer, TradeOffer inviteeOffer,
        bool initiatorReady, bool inviteeReady, bool initiatorConfirmed, bool inviteeConfirmed)
    {
        TradeId = tradeId;
        Initiator = initiator;
        Invitee = invitee;
        CurrencyCode = currencyCode;
        ExpiresAt = expiresAt;
        Revision = revision;
        OfferRevision = offerRevision;
        Status = status;
        InitiatorOffer = initiatorOffer;
        InviteeOffer = inviteeOffer;
        InitiatorReady = initiatorReady;
        InviteeReady = inviteeReady;
        InitiatorConfirmed = initiatorConfirmed;
        InviteeConfirmed = inviteeConfirmed;
    }

    public static TradeNegotiation Invite(Guid tradeId, TradeParticipant initiator, TradeParticipant invitee,
        string currencyCode, DateTimeOffset now, DateTimeOffset expiresAt)
    {
        if (tradeId == Guid.Empty || !ValidParticipant(initiator) || !ValidParticipant(invitee)
            || initiator.CharacterId == invitee.CharacterId || initiator.AccountId == invitee.AccountId
            || string.IsNullOrWhiteSpace(currencyCode) || currencyCode.Length > 64
            || !currencyCode.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-') || expiresAt <= now)
            throw new TradeRuleException("invalid_trade_invitation");
        return new(tradeId, initiator, invitee, currencyCode, expiresAt, 1, 1,
            TradeNegotiationStatus.Invited, TradeOffer.Empty, TradeOffer.Empty, false, false, false, false);
    }

    public TradeNegotiation Accept(TradeParticipant actor, long expectedRevision, DateTimeOffset now)
    {
        bool isInitiator = CheckActor(actor, expectedRevision, now);
        if (isInitiator || Status != TradeNegotiationStatus.Invited)
            throw new TradeRuleException("trade_invitation_not_available");
        return Next(TradeNegotiationStatus.Negotiating);
    }

    public TradeNegotiation ReplaceOffer(TradeParticipant actor, long expectedRevision, TradeOffer offer, DateTimeOffset now)
    {
        ArgumentNullException.ThrowIfNull(offer);
        bool isInitiator = CheckActor(actor, expectedRevision, now);
        RequireNegotiating();
        TradeOffer other = isInitiator ? InviteeOffer : InitiatorOffer;
        if (other.Items.Select(item => item.ItemId).Intersect(offer.Items.Select(item => item.ItemId)).Any())
            throw new TradeRuleException("duplicate_trade_item");
        return Next(Status, Increment(OfferRevision), isInitiator ? offer : InitiatorOffer,
            isInitiator ? InviteeOffer : offer, false, false, false, false);
    }

    // Persist this transition ONLY together with successfully reserving this participant's assets.
    public TradeNegotiation Ready(TradeParticipant actor, long expectedRevision, long expectedOfferRevision, DateTimeOffset now)
    {
        bool isInitiator = CheckActor(actor, expectedRevision, now);
        RequireNegotiating();
        CheckOfferRevision(expectedOfferRevision);
        if (InitiatorOffer.IsEmpty && InviteeOffer.IsEmpty)
            throw new TradeRuleException("empty_trade");
        if (isInitiator ? InitiatorReady : InviteeReady) return this;
        return Next(Status, initiatorReady: isInitiator || InitiatorReady, inviteeReady: !isInitiator || InviteeReady);
    }

    // Persist together with releasing both reservations; earlier confirmations become invalid.
    public TradeNegotiation WithdrawReady(TradeParticipant actor, long expectedRevision, DateTimeOffset now)
    {
        CheckActor(actor, expectedRevision, now);
        RequireNegotiating();
        return Next(Status, initiatorReady: false, inviteeReady: false, initiatorConfirmed: false, inviteeConfirmed: false);
    }

    // The final confirmation and asset settlement must be one storage transaction in T2.
    public TradeNegotiation Confirm(TradeParticipant actor, long expectedRevision, long expectedOfferRevision, DateTimeOffset now)
    {
        bool isInitiator = CheckActor(actor, expectedRevision, now);
        RequireNegotiating();
        CheckOfferRevision(expectedOfferRevision);
        if (!InitiatorReady || !InviteeReady) throw new TradeRuleException("trade_not_ready");
        if (isInitiator ? InitiatorConfirmed : InviteeConfirmed) return this;
        bool first = isInitiator || InitiatorConfirmed;
        bool second = !isInitiator || InviteeConfirmed;
        return Next(first && second ? TradeNegotiationStatus.Agreed : Status,
            initiatorConfirmed: first, inviteeConfirmed: second);
    }

    // Until asset settlement, cancellation/expiry must release reservations in the same transaction.
    public TradeNegotiation Cancel(TradeParticipant actor, long expectedRevision, DateTimeOffset now)
    {
        CheckParticipant(actor);
        CheckRevision(expectedRevision);
        RequireOpen();
        return Close(now >= ExpiresAt ? TradeNegotiationStatus.Expired : TradeNegotiationStatus.Cancelled);
    }

    // Called by the server's expiry worker, never on a client-supplied clock.
    public TradeNegotiation Expire(long expectedRevision, DateTimeOffset now)
    {
        CheckRevision(expectedRevision);
        RequireOpen();
        if (now < ExpiresAt) throw new TradeRuleException("trade_not_expired");
        return Close(TradeNegotiationStatus.Expired);
    }

    private TradeNegotiation Close(TradeNegotiationStatus status) =>
        Next(status, initiatorReady: false, inviteeReady: false, initiatorConfirmed: false, inviteeConfirmed: false);

    private bool CheckActor(TradeParticipant actor, long expectedRevision, DateTimeOffset now)
    {
        bool first = CheckParticipant(actor);
        CheckRevision(expectedRevision);
        RequireOpen();
        if (now >= ExpiresAt) throw new TradeRuleException("trade_expired");
        return first;
    }

    private bool CheckParticipant(TradeParticipant actor)
    {
        if (actor == Initiator) return true;
        if (actor == Invitee) return false;
        throw new TradeRuleException("trade_participant_required");
    }

    private void CheckRevision(long expected)
    {
        if (expected != Revision) throw new TradeRuleException("trade_revision_conflict");
    }

    private void CheckOfferRevision(long expected)
    {
        if (expected != OfferRevision) throw new TradeRuleException("trade_offer_revision_conflict");
    }

    private void RequireNegotiating()
    {
        if (Status != TradeNegotiationStatus.Negotiating) throw new TradeRuleException("trade_not_negotiating");
    }

    private void RequireOpen()
    {
        if (Status is TradeNegotiationStatus.Cancelled or TradeNegotiationStatus.Expired)
            throw new TradeRuleException("trade_closed");
    }

    private static bool ValidParticipant(TradeParticipant participant) => participant is not null
        && participant.CharacterId != Guid.Empty && !string.IsNullOrWhiteSpace(participant.AccountId)
        && participant.AccountId.Length <= 128 && !participant.AccountId.Any(char.IsControl);

    private static long Increment(long value) => value < long.MaxValue ? value + 1
        : throw new TradeRuleException("trade_revision_exhausted");

    private TradeNegotiation Next(TradeNegotiationStatus status, long? offerRevision = null,
        TradeOffer? initiatorOffer = null, TradeOffer? inviteeOffer = null,
        bool? initiatorReady = null, bool? inviteeReady = null, bool? initiatorConfirmed = null, bool? inviteeConfirmed = null) =>
        new(TradeId, Initiator, Invitee, CurrencyCode, ExpiresAt, Increment(Revision), offerRevision ?? OfferRevision,
            status, initiatorOffer ?? InitiatorOffer, inviteeOffer ?? InviteeOffer,
            initiatorReady ?? InitiatorReady, inviteeReady ?? InviteeReady,
            initiatorConfirmed ?? InitiatorConfirmed, inviteeConfirmed ?? InviteeConfirmed);
}
