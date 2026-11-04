namespace ProjectRpg.Backend.Application;

// Observation only. Implementations must not participate in reward transactions.
public enum DungeonSettlementPhase { Started, ReceiptCommitted, Completed, Failed, Requeued }
public sealed record DungeonSettlementObservation(Guid SessionId, int Attempt, DungeonSettlementPhase Phase,
    Guid? CharacterId = null, Guid? TransactionRequestId = null);
public interface IDungeonSettlementObserver
{
    void Observe(DungeonSettlementObservation observation);
}
