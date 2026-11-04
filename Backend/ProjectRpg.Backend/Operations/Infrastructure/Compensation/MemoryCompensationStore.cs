using ProjectRpg.Backend.Data;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Compensation;

namespace ProjectRpg.Backend.Operations.Infrastructure.Compensation;

public sealed class MemoryCompensationStore(InMemoryGameRepository game, ICompensationPolicy policy, TimeProvider clock)
    : ICompensationCommitter, IOperationActionReader
{
    public Task<Result<ActionCommit>> CommitAsync(CompensationCommand command, CancellationToken ct) =>
        Task.FromResult(game.CommitOperationsCompensation(command, policy, clock.GetUtcNow(), ct));
    public Task<Result<OperationAction>> FindAsync(Guid requestId, CancellationToken ct) =>
        Task.FromResult(game.FindOperationsAction(requestId, ct));
    public Task<Result<Page<OperationAction>>> SearchAsync(Guid characterId, PageRequest page, CancellationToken ct) =>
        Task.FromResult(game.SearchOperationsActions(characterId, page, ct));
}
