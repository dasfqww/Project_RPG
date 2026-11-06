using ProjectRpg.Backend.Data;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.ServiceControl;

namespace ProjectRpg.Backend.Operations.ServiceControl;

public sealed class MemoryServiceControlStore(InMemoryGameRepository game, TimeProvider clock) : IServiceControlReader, IServiceControlCommitter
{
    public Task<Result<ServiceSnapshot>> ReadAsync(CancellationToken ct) { ct.ThrowIfCancellationRequested(); return Task.FromResult(Result<ServiceSnapshot>.Success(game.ReadServiceControl())); }
    public Task<Result<ServiceReceipt>> FindActionAsync(Guid id, CancellationToken ct) { ct.ThrowIfCancellationRequested(); return Task.FromResult(game.ReadServiceAction(id)); }
    public Task<Result<ServiceCommit>> CommitAsync(ServiceCommand command, CancellationToken ct) => Task.FromResult(game.CommitServiceControl(command, clock.GetUtcNow(), ct));
}
