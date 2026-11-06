using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain.Trading;

internal static class TradeSessionRules
{
    internal static string Fingerprint(TradeSessionCommand command) => Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(command))));
    internal static Guid ReadyReservationId(Guid requestId) => new(SHA256.HashData(Encoding.UTF8.GetBytes("trade_ready|" + requestId.ToString("D"))).AsSpan(0, 16));
    internal static bool Participant(TradeNegotiation s, TradeParticipant p) => p == s.Initiator || p == s.Invitee;
    internal static void Validate(TradeSessionCommand c)
    {
        if (c is null || c.RequestId == Guid.Empty || c.TradeId == Guid.Empty || c.Actor is null || c.Actor.CharacterId == Guid.Empty
            || string.IsNullOrWhiteSpace(c.Actor.AccountId) || c.Actor.AccountId.Length > 20 || !Enum.IsDefined(c.Kind)
            || (c.Kind == TradeCommandKind.Invite ? c.Invitee is null || string.IsNullOrWhiteSpace(c.CurrencyCode)
                || c.ExpectedRevision != 0 || c.ExpectedOfferRevision != 0 : c.Invitee is not null || c.CurrencyCode is not null)
            || (c.Kind == TradeCommandKind.ReplaceOffer ? c.Offer is null : c.Offer is not null))
            throw new TradeRuleException("invalid_trade_command");
    }
}
