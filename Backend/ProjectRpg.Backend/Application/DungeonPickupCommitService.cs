using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

// Bind the existing game and item singleton repositories; never accept a commit body here.
public sealed class DungeonPickupCommitService(IGameRepository gameRepository, IItemRepository itemRepository,
    TimeProvider clock, ILogger<DungeonPickupCommitService> logger)
{
    public async Task<DungeonPickupCommitResult> CommitAsync(Guid sessionId, string serverId, Guid requestId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!DungeonPickupIntentRules.ValidScope(sessionId, serverId) || requestId == Guid.Empty)
            return new(DungeonPickupCommitStatus.InvalidRequest);
        try
        {
            return (gameRepository, itemRepository) switch
            {
                (InMemoryGameRepository game, InMemoryItemRepository items) =>
                    game.CommitPickup(items, sessionId, serverId, requestId, clock, cancellationToken),
                (PostgresGameRepository game, PostgresItemRepository items) =>
                    await game.CommitPickupAsync(items, sessionId, serverId, requestId, clock, cancellationToken),
                _ => throw new InvalidOperationException("Pickup repositories must use the same storage provider.")
            };
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
