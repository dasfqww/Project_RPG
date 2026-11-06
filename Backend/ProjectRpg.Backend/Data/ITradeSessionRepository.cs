using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

// Internal port. Expire and expired scans are server-only; player identities require authentication.
public interface ITradeSessionRepository
{
    Task<TradeSessionResult> ExecuteAsync(TradeSessionCommand command, CancellationToken ct);
    Task<TradeStoredSession?> FindAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct);
    Task<TradeStoredSession?> FindActiveAsync(TradeParticipant actor, CancellationToken ct);
    Task<IReadOnlyList<TradeStoredSession>> FindExpiredAsync(int limit, CancellationToken ct);
}
