using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IItemRepository
{
    Task<ItemRecord?> FindAsync(
        Guid itemId,
        CancellationToken cancellationToken);

    Task<IReadOnlyList<ItemRecord>> FindByOwnerAsync(
        ItemOwnerRef owner,
        bool includeTerminal,
        int limit,
        CancellationToken cancellationToken);

    Task<ItemRepositoryCommitResult?> TryGetCommitResultAsync(
        Guid requestId,
        CancellationToken cancellationToken);

    Task<ItemRepositoryCommitResult> CommitAsync(
        ItemRepositoryCommitRequest request,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken);
}
