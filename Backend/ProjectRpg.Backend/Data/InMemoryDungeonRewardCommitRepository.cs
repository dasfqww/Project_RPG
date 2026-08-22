using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryDungeonRewardCommitRepository(
    InMemoryTransactionGate transactionGate,
    InMemoryEconomyRepository economyRepository,
    InMemoryItemRepository itemRepository) : IDungeonRewardCommitRepository
{
    public Task<DungeonRewardCommitResult> CommitAsync(
        DungeonRewardCommitBatch batch,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (transactionGate.SyncRoot)
        {
            InMemoryEconomyRepository.Snapshot economySnapshot =
                economyRepository.CaptureSnapshot();
            InMemoryItemRepository.Snapshot itemSnapshot =
                itemRepository.CaptureSnapshot();
            try
            {
                IReadOnlyList<CurrencyTransactionResult> currencyResults =
                    economyRepository.CommitBatchAsync(
                            batch.CurrencyEntries,
                            committedAt,
                            cancellationToken)
                        .GetAwaiter()
                        .GetResult();
                CurrencyTransactionResult? failedCurrency =
                    currencyResults.FirstOrDefault(result => result.Status is not (
                        CurrencyTransactionStatus.Committed
                        or CurrencyTransactionStatus.AlreadyCommitted));
                if (failedCurrency is not null)
                {
                    Restore(economySnapshot, itemSnapshot);
                    if (failedCurrency.Status == CurrencyTransactionStatus.InternalError)
                    {
                        throw new InvalidOperationException(
                            "The atomic currency batch returned InternalError.");
                    }

                    return Task.FromResult(new DungeonRewardCommitResult(
                        false,
                        $"Currency settlement failed with {failedCurrency.Status} for character '{failedCurrency.CharacterId}'."));
                }

                foreach (ItemRepositoryCommitRequest request in batch.ItemRequests)
                {
                    ItemRepositoryCommitResult result = itemRepository.CommitAsync(
                            request,
                            committedAt,
                            cancellationToken)
                        .GetAwaiter()
                        .GetResult();
                    if (result.Status is ItemRepositoryCommitStatus.Committed
                        || (result.Status == ItemRepositoryCommitStatus.AlreadyCommitted
                            && IsSameCommand(result, request)))
                    {
                        continue;
                    }

                    Restore(economySnapshot, itemSnapshot);
                    if (result.Status == ItemRepositoryCommitStatus.InternalError)
                    {
                        throw new InvalidOperationException(
                            "The atomic item batch returned InternalError.");
                    }

                    return Task.FromResult(new DungeonRewardCommitResult(
                        false,
                        $"Item settlement failed with {result.Status} for actor '{result.Actor.OwnerId}'."));
                }

                return Task.FromResult(new DungeonRewardCommitResult(true, null));
            }
            catch
            {
                Restore(economySnapshot, itemSnapshot);
                throw;
            }

            void Restore(
                InMemoryEconomyRepository.Snapshot economy,
                InMemoryItemRepository.Snapshot items)
            {
                economyRepository.RestoreSnapshot(economy);
                itemRepository.RestoreSnapshot(items);
            }
        }
    }

    private static bool IsSameCommand(
        ItemRepositoryCommitResult result,
        ItemRepositoryCommitRequest request)
    {
        return string.Equals(
                result.Operation,
                request.Operation,
                StringComparison.Ordinal)
            && string.Equals(
                result.CommandFingerprint,
                request.CommandFingerprint,
                StringComparison.Ordinal)
            && result.Actor == request.Actor;
    }
}
