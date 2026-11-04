using ProjectRpg.Backend.Application;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsSettlementObserver(OperationsEventRecorder events) : IDungeonSettlementObserver
{
    public void Observe(DungeonSettlementObservation observation) => events.RecordSettlement(observation);
}
