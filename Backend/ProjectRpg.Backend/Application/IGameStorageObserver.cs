namespace ProjectRpg.Backend.Application;

public enum GameStorageOperation { ItemReceiptRead, ItemCommit, CurrencyReceiptRead, CurrencyCommit }
public enum GameStorageOutcome { Returned, Faulted, Canceled }

/// <summary>A repository invocation, not proof of a database commit or rollback.</summary>
public interface IGameStorageObserver
{
    IGameStorageObservation? Begin(GameStorageOperation operation, Guid transactionRequestId);
}

public interface IGameStorageObservation
{
    void End(GameStorageOutcome outcome);
}
