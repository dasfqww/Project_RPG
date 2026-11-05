using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain.Trading;

public enum TradeReservationStatus { Active, Released, Expired }

// A server-side command, not a public HTTP contract. Each offer generation gets a new reservation ID.
public sealed record TradeReservationCommand(Guid ReservationId, Guid TradeId, long OfferRevision,
    TradeParticipant Participant, string CurrencyCode, TradeOffer Offer, DateTimeOffset ExpiresAt);

public sealed record TradeAssetReservation(TradeReservationCommand Command, string Fingerprint,
    CurrencyOwnerRef? WalletOwner, TradeReservationStatus Status)
{
    public TradeAssetReservation At(DateTimeOffset now) => Status == TradeReservationStatus.Active && Command.ExpiresAt <= now
        ? this with { Status = TradeReservationStatus.Expired } : this;
}

internal sealed record TradeReservationPayload(Guid ReservationId, Guid TradeId, long OfferRevision,
    TradeParticipant Participant, string CurrencyCode, TradeOfferedItem[] Items, long CurrencyAmount, DateTimeOffset ExpiresAt);

internal static class TradeReservationRules
{
    internal static TradeReservationCommand Validate(TradeReservationCommand command)
    {
        if (command is null || command.ReservationId == Guid.Empty || command.TradeId == Guid.Empty
            || command.OfferRevision <= 0 || command.OfferRevision == long.MaxValue || command.Participant is null
            || command.Participant.CharacterId == Guid.Empty || string.IsNullOrWhiteSpace(command.Participant.AccountId)
            || command.Participant.AccountId.Length > 128 || command.Participant.AccountId.Any(char.IsControl)
            || command.Offer is null || string.IsNullOrWhiteSpace(command.CurrencyCode) || command.CurrencyCode.Length > 64
            || !command.CurrencyCode.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-'))
            throw new TradeRuleException("invalid_reservation");
        return command with { ExpiresAt = command.ExpiresAt.ToUniversalTime() };
    }

    internal static string Serialize(TradeReservationCommand command) => JsonSerializer.Serialize(new TradeReservationPayload(
        command.ReservationId, command.TradeId, command.OfferRevision, command.Participant, command.CurrencyCode,
        command.Offer.Items.ToArray(), command.Offer.CurrencyAmount, command.ExpiresAt));

    internal static TradeReservationCommand Deserialize(string json)
    {
        var value = JsonSerializer.Deserialize<TradeReservationPayload>(json) ?? throw new InvalidDataException("Missing reservation payload.");
        return Validate(new(value.ReservationId, value.TradeId, value.OfferRevision, value.Participant,
            value.CurrencyCode, new TradeOffer(value.Items, value.CurrencyAmount), value.ExpiresAt));
    }

    internal static string Fingerprint(TradeReservationCommand command) =>
        Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(Serialize(command))));

    internal static TradeAssetReservation Replay(TradeAssetReservation stored, string fingerprint, DateTimeOffset now)
    {
        if (stored.Fingerprint != fingerprint) throw new TradeRuleException("reservation_idempotency_conflict");
        return stored.At(now);
    }
}
