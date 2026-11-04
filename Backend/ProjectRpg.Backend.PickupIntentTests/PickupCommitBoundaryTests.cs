using Microsoft.Extensions.Logging.Abstractions;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

static class PickupCommitBoundaryTests
{
    public static async Task Run()
    {
        var port = new RecordingCommitRepository();
        var service = new DungeonPickupCommitService(port, NullLogger<DungeonPickupCommitService>.Instance);
        var session = Guid.NewGuid(); var request = Guid.NewGuid();
        using var cancellation = new CancellationTokenSource();
        foreach (var status in Enum.GetValues<DungeonPickupCommitStatus>())
        {
            port.Result = new(status);
            var result = await service.CommitAsync(session, "server", request, cancellation.Token);
            Checks.Require(ReferenceEquals(result, port.Result) && port.Scope == (session, "server", request)
                && port.Cancellation == cancellation.Token, "Custom commit provider lost scope, outcome or cancellation.");
        }
        int calls = port.Calls;
        Checks.Require((await service.CommitAsync(Guid.Empty, "server", request, default)).Status
            == DungeonPickupCommitStatus.InvalidRequest && port.Calls == calls, "Invalid scope reached storage.");
        cancellation.Cancel();
        try { await service.CommitAsync(session, "server", request, cancellation.Token); throw new TestFailure("Cancellation swallowed."); }
        catch (OperationCanceledException) { }
        Checks.Require(port.Calls == calls, "Pre-cancelled request reached storage.");
        port.Error = new OperationCanceledException();
        try { await service.CommitAsync(session, "server", request, default); throw new TestFailure("Provider cancellation swallowed."); }
        catch (OperationCanceledException) { }
        port.Error = new InvalidOperationException("private storage error");
        var failed = await service.CommitAsync(session, "server", request, default);
        Checks.Require(failed.Status == DungeonPickupCommitStatus.InternalError && failed.Receipt is null,
            "Storage failure escaped the public boundary.");
        Console.WriteLine("PASS pickup application boundary: independent provider, all outcomes, scope, cancellation and failure.");
    }

    private sealed class RecordingCommitRepository : IDungeonPickupCommitRepository
    {
        public DungeonPickupCommitResult Result = new(DungeonPickupCommitStatus.NotFound);
        public Exception? Error;
        public int Calls;
        public (Guid, string, Guid) Scope;
        public CancellationToken Cancellation;
        public Task<DungeonPickupCommitResult> CommitAsync(Guid sessionId, string serverId, Guid requestId,
            CancellationToken cancellationToken)
        {
            ++Calls; Scope = (sessionId, serverId, requestId); Cancellation = cancellationToken;
            return Error is null ? Task.FromResult(Result) : Task.FromException<DungeonPickupCommitResult>(Error);
        }
    }
}
