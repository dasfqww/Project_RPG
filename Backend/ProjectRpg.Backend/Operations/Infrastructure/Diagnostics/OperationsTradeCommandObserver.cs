using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Operations.Hosting;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsTradeCommandObserver(IHttpContextAccessor context, OperationsEventRecorder events) : ITradeCommandObserver
{
    public void Observe(TradeCommandObservation observation)
    {
        if (context.HttpContext?.Items.TryGetValue(GameHttpDiagnostics.RequestIdItemKey,out object? value)==true
            &&value is Guid id&&id!=Guid.Empty) events.RecordTradeCommand(id,observation);
    }
}
