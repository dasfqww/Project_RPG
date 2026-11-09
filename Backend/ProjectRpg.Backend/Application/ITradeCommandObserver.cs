using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Application;

// A persisted command result may be an idempotent replay. Observation never claims a new exchange.
public sealed record TradeCommandObservation(Guid TradeId, Guid CommandRequestId, Guid CharacterId,
    TradeCommandKind Kind, TradeNegotiationStatus State, bool CompletedReceipt, bool StoredRefusal);

public interface ITradeCommandObserver
{
    void Observe(TradeCommandObservation observation);
}
