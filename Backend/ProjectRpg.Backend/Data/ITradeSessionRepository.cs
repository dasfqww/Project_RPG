using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

// Internal port. Expire and expired scans are server-only; player identities require authentication.
public interface ITradeSessionRepository
{
    // Exact name only, at most one public candidate. The invitation transaction rechecks access.
    Task<IReadOnlyList<TradeTarget>> FindTargetsAsync(TradeParticipant actor, string name, CancellationToken ct);
    Task<TradeSessionResult> ExecuteAsync(TradeSessionCommand command, CancellationToken ct);
    Task<TradeStoredSession?> FindAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct);
    Task<TradeStoredSession?> FindActiveAsync(TradeParticipant actor, CancellationToken ct);
    Task<IReadOnlyList<TradeStoredSession>> FindExpiredAsync(int limit, CancellationToken ct);
    Task<TradePreview?> PreviewAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct);
    Task<TradeAssets?> AssetsAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct);
}
