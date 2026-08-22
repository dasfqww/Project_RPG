using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed record DungeonRewardCommitBatch(
    IReadOnlyList<CurrencyBatchEntry> CurrencyEntries,
    IReadOnlyList<ItemRepositoryCommitRequest> ItemRequests);

public sealed record DungeonRewardCommitResult(
    bool Succeeded,
    string? Error);

public interface IDungeonRewardCommitRepository
{
    Task<DungeonRewardCommitResult> CommitAsync(
        DungeonRewardCommitBatch batch,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken);
}
