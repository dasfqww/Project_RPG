using ProjectRpg.Backend.Domain;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.ServiceControl;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    private ServiceSnapshot _serviceControl = ServiceSnapshot.Initial;
    private readonly Dictionary<Guid, (string Hash, ServiceReceipt Receipt)> _serviceActions = [];
    private void EnsureServiceOpen() { if (_serviceControl.Mode != "Open") throw new ServiceDrainingException(); }
    private void EnsureAdmissionAllowed(string account, DateTimeOffset now)
    { EnsureServiceOpen(); EnsureAccountAllowed(account, now); }
    internal ServiceSnapshot ReadServiceControl() { lock (_dungeonGate) return _serviceControl; }
    internal Result<ServiceReceipt> ReadServiceAction(Guid id)
    {
        lock (_dungeonGate) return _serviceActions.TryGetValue(id, out var action)
            ? Result<ServiceReceipt>.Success(action.Receipt) : Result<ServiceReceipt>.Fail(ServiceFailures.Missing);
    }
    internal Result<ServiceCommit> CommitServiceControl(ServiceCommand command, DateTimeOffset now, CancellationToken ct)
    {
        lock (_dungeonGate)
        {
            ct.ThrowIfCancellationRequested();
            var id = command.Intent.RequestId;
            if (_operationsActions.ContainsKey(id) || _restrictionActions.ContainsKey(id)) return Result<ServiceCommit>.Fail(ServiceFailures.Conflict);
            if (_serviceActions.TryGetValue(id, out var prior)) return prior.Hash == command.InputHash
                ? Result<ServiceCommit>.Success(new("Replayed", prior.Receipt)) : Result<ServiceCommit>.Fail(ServiceFailures.Conflict);
            if (_serviceControl.Revision != command.Intent.ExpectedRevision) return Result<ServiceCommit>.Fail(ServiceFailures.Revision);
            var receipt = command.Receipt(now);
            // Allocate/add the immutable audit first; assignment cannot throw. Both are
            // published under the same gate as every admission, never as two commits.
            _serviceActions.Add(id, (command.InputHash, receipt));
            _serviceControl = receipt.Result;
            return Result<ServiceCommit>.Success(new("Committed", receipt));
        }
    }
}
