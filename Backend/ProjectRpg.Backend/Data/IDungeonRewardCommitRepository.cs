using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed record DungeonRewardCommitBatch(
    IReadOnlyList<CurrencyBatchEntry> CurrencyEntries,
    IReadOnlyList<ItemRepositoryCommitRequest> ItemRequests,
    IReadOnlyList<int>? ItemMaxStackSizes = null,
    IReadOnlyDictionary<Guid, IReadOnlyList<int>>? ItemMaxStackSizesByRequest = null)
{
    public IReadOnlyList<int>? StackSizesFor(ItemRepositoryCommitRequest request)
    {
        if (ItemMaxStackSizesByRequest is null) return ItemMaxStackSizes;
        if (!ItemMaxStackSizesByRequest.TryGetValue(request.RequestId, out IReadOnlyList<int>? sizes)
            || sizes.Count != request.Mutations.Count)
            throw new ArgumentException("Missing or mismatched recipient reward stack limits.");
        return sizes;
    }
}

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
