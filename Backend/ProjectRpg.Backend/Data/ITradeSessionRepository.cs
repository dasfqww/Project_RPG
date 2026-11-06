using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

// No HTTP endpoint/DI exposure yet. Expire is a server-worker command; other actor identities require authentication.
public interface ITradeSessionRepository
{
    Task<TradeSessionResult> ExecuteAsync(TradeSessionCommand command, CancellationToken ct);
    Task<TradeStoredSession?> FindAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct);
}
