using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

// Provider composition preserves the existing singleton stores and transaction boundaries.
public sealed class InMemoryDungeonPickupCommitRepository(InMemoryGameRepository game,
    InMemoryItemRepository items, TimeProvider clock) : IDungeonPickupCommitRepository
{
    public Task<DungeonPickupCommitResult> CommitAsync(Guid sessionId, string serverId, Guid requestId,
        CancellationToken cancellationToken) => Task.FromResult(
            game.CommitPickup(items, sessionId, serverId, requestId, clock, cancellationToken));
}

public sealed class PostgresDungeonPickupCommitRepository(PostgresGameRepository game,
    PostgresItemRepository items, TimeProvider clock) : IDungeonPickupCommitRepository
{
    public Task<DungeonPickupCommitResult> CommitAsync(Guid sessionId, string serverId, Guid requestId,
        CancellationToken cancellationToken) =>
        game.CommitPickupAsync(items, sessionId, serverId, requestId, clock, cancellationToken);
}
