using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Operations.Hosting;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsGameTransactionObserver(IHttpContextAccessor context, OperationsEventRecorder events) : IGameTransactionObserver
{
    public void Observe(GameTransactionObservation observation)
    {
        // Only the game HTTP boundary creates this item. Background calls are not a fictitious HTTP attempt.
        if (context.HttpContext?.Items.TryGetValue(GameHttpDiagnostics.RequestIdItemKey, out object? value) == true
            && value is Guid id && id != Guid.Empty)
            events.RecordGameTransaction(id, observation);
    }
}
