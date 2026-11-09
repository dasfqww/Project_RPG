namespace ProjectRpg.Backend.Application;

/// <summary>Runs the original call exactly once and isolates optional diagnostics at both ends.</summary>
internal static class GameStorageCall
{
    internal static async Task<T> RunAsync<T>(IGameStorageObserver? observer, GameStorageOperation operation,
        Guid transactionRequestId, Func<Task<T>> call)
    {
        IGameStorageObservation? observation = null;
        try { observation = observer?.Begin(operation, transactionRequestId); }
        catch (Exception) { /* Starting diagnostics cannot prevent the repository call. */ }
        var outcome = GameStorageOutcome.Faulted;
        try
        {
            T result = await call();
            outcome = GameStorageOutcome.Returned;
            return result;
        }
        catch (OperationCanceledException) { outcome = GameStorageOutcome.Canceled; throw; }
        finally
        {
            try { observation?.End(outcome); }
            catch (Exception) { /* Preserve the original result or exception, including uncertain writes. */ }
        }
    }
}
