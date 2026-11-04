using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

// The application depends on an atomic operation, independent of the storage provider.
public sealed class DungeonPickupCommitService(IDungeonPickupCommitRepository repository,
    ILogger<DungeonPickupCommitService> logger)
{
    public async Task<DungeonPickupCommitResult> CommitAsync(Guid sessionId, string serverId, Guid requestId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!DungeonPickupIntentRules.ValidScope(sessionId, serverId) || requestId == Guid.Empty)
            return new(DungeonPickupCommitStatus.InvalidRequest);
        try
        {
            return await repository.CommitAsync(sessionId, serverId, requestId, cancellationToken);
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception exception)
        {
            // Never log a token, frozen body or database exception details.
            logger.LogError("Dungeon pickup commit {RequestId} failed ({ExceptionType}).", requestId, exception.GetType().Name);
            return new(DungeonPickupCommitStatus.InternalError);
        }
    }
}
