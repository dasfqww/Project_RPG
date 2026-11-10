using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

// The application depends on an atomic operation, independent of the storage provider.
public sealed class DungeonPickupCommitService(IDungeonPickupCommitRepository repository,
    ILogger<DungeonPickupCommitService> logger, IGameTransactionObserver? observer = null,
    IGameStorageObserver? storageObserver = null)
{
    public async Task<DungeonPickupCommitResult> CommitAsync(Guid sessionId, string serverId, Guid requestId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!DungeonPickupIntentRules.ValidScope(sessionId, serverId) || requestId == Guid.Empty)
            return new(DungeonPickupCommitStatus.InvalidRequest);
        try
        {
            var result = await GameStorageCall.RunAsync(storageObserver, GameStorageOperation.DungeonPickupCommit,
                requestId, () => repository.CommitAsync(sessionId, serverId, requestId, cancellationToken));
            Observe(result, sessionId, requestId);
            return result;
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception exception)
        {
            // Never log a token, frozen body or database exception details.
            logger.LogError("Dungeon pickup commit {RequestId} failed ({ExceptionType}).", requestId, exception.GetType().Name);
            return new(DungeonPickupCommitStatus.InternalError);
        }
    }

    private void Observe(DungeonPickupCommitResult result, Guid sessionId, Guid requestId)
    {
        if (observer is null) return;
        try
        {
            if (result.Status is not (DungeonPickupCommitStatus.Committed or DungeonPickupCommitStatus.AlreadyCommitted)
                || result.Receipt is not { Records.Count: 1 } receipt || receipt.RequestId != requestId
                || (result.Status, receipt.Status) is not
                    ((DungeonPickupCommitStatus.Committed, ItemRepositoryCommitStatus.Committed)
                    or (DungeonPickupCommitStatus.AlreadyCommitted, ItemRepositoryCommitStatus.AlreadyCommitted))
                || receipt.Actor.Type != ItemOwnerType.Character
                || !Guid.TryParseExact(receipt.Actor.OwnerId, "D", out Guid character) || character == Guid.Empty
                || receipt.Records[0].Owner != receipt.Actor || receipt.Records[0].ItemId == Guid.Empty) return;
            // A successful repository result verified this exact frozen intent/session and committed its receipt.
            observer.Observe(new(GameTransactionKind.DungeonPickup,
                result.Status == DungeonPickupCommitStatus.Committed ? GameTransactionDisposition.Committed : GameTransactionDisposition.Replayed,
                receipt.RequestId, character, sessionId, receipt.Records[0].ItemId));
        }
        catch (Exception) { /* Optional observation cannot turn an atomic pickup/replay into InternalError. */ }
    }
}
