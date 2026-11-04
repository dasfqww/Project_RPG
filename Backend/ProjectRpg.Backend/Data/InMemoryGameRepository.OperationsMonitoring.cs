using ProjectRpg.Backend.Domain;
using ProjectRpg.Operations.Core.Monitoring;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    internal SettlementHealth ReadOperationsSettlementHealth()
    {
        lock (_dungeonGate)
        {
            var rows=_dungeonRewardSettlements.Values;
            return new(rows.LongCount(s=>s.State==DungeonRewardSettlementState.Pending), rows.LongCount(s=>s.State==DungeonRewardSettlementState.Processing),
                rows.LongCount(s=>s.State==DungeonRewardSettlementState.Failed),
                rows.Where(s=>s.State is DungeonRewardSettlementState.Pending or DungeonRewardSettlementState.Processing).Select(s=>(DateTimeOffset?)s.CreatedAt).Min(),
                rows.Where(s=>s.State==DungeonRewardSettlementState.Failed).Select(s=>(DateTimeOffset?)s.UpdatedAt).Max());
        }
    }
}
