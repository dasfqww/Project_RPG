namespace ProjectRpg.Backend.Application;

public enum GameTransactionKind { Item, Currency }
public enum GameTransactionDisposition { Committed, Replayed }

// Only confirmed repository results cross this boundary. No caller text or business payloads.
public sealed record GameTransactionObservation(GameTransactionKind Kind, GameTransactionDisposition Disposition,
    Guid TransactionRequestId, Guid? CharacterId = null);

/// <summary>Optional observation after commit/replay; never participates in the transaction.</summary>
public interface IGameTransactionObserver
{
    void Observe(GameTransactionObservation observation);
}
