using ProjectRpg.Backend.Data;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Restrictions;

namespace ProjectRpg.Backend.Operations.Restrictions;

public sealed class MemoryRestrictionStore(InMemoryGameRepository game,TimeProvider clock):IAccountRestrictionReader,IAccountRestrictionCommitter
{
    public Task<Result<RestrictionCommit>> CommitAsync(RestrictionCommand command,CancellationToken ct)=>Task.FromResult(game.CommitOperationsRestriction(command,clock.GetUtcNow(),ct));
    public Task<Result<RestrictionSnapshot>> FindAsync(string id,CancellationToken ct){ct.ThrowIfCancellationRequested();return Task.FromResult(game.ReadOperationsRestriction(id,clock.GetUtcNow()));}
    public Task<Result<RestrictionReceipt>> FindActionAsync(Guid id,CancellationToken ct){ct.ThrowIfCancellationRequested();return Task.FromResult(game.ReadOperationsRestrictionAction(id));}
    public Task<Result<Page<RestrictionReceipt>>> SearchActionsAsync(string id,PageRequest page,CancellationToken ct)
    {ct.ThrowIfCancellationRequested();return Task.FromResult(game.ReadOperationsRestrictionActions(id,page));}
}
