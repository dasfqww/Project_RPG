namespace ProjectRpg.Backend.Domain.Trading;

public enum TradeCommandKind { Invite, Accept, ReplaceOffer, Ready, Withdraw, Confirm, Cancel, Expire }

// Internal application command. Actor MUST come from authenticated account/character ownership, never a client DTO.
public sealed record TradeSessionCommand(Guid RequestId, Guid TradeId, TradeParticipant Actor,
    TradeCommandKind Kind, long ExpectedRevision = 0, long ExpectedOfferRevision = 0,
    TradeParticipant? Invitee = null, string? CurrencyCode = null, TradeOffer? Offer = null);

public sealed record TradeStoredSession(TradeNegotiation State, Guid? InitiatorReservation,
    Guid? InviteeReservation, bool Completed = false, TradeExchangePlan? Exchange = null);
public sealed record TradeSessionResult(Guid RequestId, TradeStoredSession Session, string? Rejection, DateTimeOffset CommittedAt);
