using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class PostgresDungeonRewardCommitRepository(
    NpgsqlDataSource dataSource,
    PostgresEconomyRepository economyRepository,
    PostgresItemRepository itemRepository,
    ILogger<PostgresDungeonRewardCommitRepository> logger)
    : IDungeonRewardCommitRepository
{
    public async Task<DungeonRewardCommitResult> CommitAsync(
        DungeonRewardCommitBatch batch,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        try
        {
            IReadOnlyList<CurrencyTransactionResult> currencyResults =
                await economyRepository.CommitBatchInTransactionAsync(
                    connection,
                    transaction,
                    batch.CurrencyEntries,
                    committedAt,
                    cancellationToken);
            CurrencyTransactionResult? failedCurrency =
                currencyResults.FirstOrDefault(result => result.Status is not (
                    CurrencyTransactionStatus.Committed
                    or CurrencyTransactionStatus.AlreadyCommitted));
            if (failedCurrency is not null)
            {
                await transaction.RollbackAsync(cancellationToken);
                if (failedCurrency.Status == CurrencyTransactionStatus.InternalError)
                {
                    throw new InvalidOperationException(
                        "The atomic currency batch returned InternalError.");
                }

                return new DungeonRewardCommitResult(
                    false,
                    $"Currency settlement failed with {failedCurrency.Status} for character '{failedCurrency.CharacterId}'.");
            }

            foreach (ItemRepositoryCommitRequest request in batch.ItemRequests
                .OrderBy(value => value.RequestId))
            {
                ItemRepositoryCommitResult result =
                    await itemRepository.CommitInTransactionAsync(
                        connection,
                        transaction,
                        request,
                        committedAt,
                        cancellationToken);
                if (result.Status is ItemRepositoryCommitStatus.Committed
                    or ItemRepositoryCommitStatus.AlreadyCommitted)
                {
                    continue;
                }

                await transaction.RollbackAsync(cancellationToken);
                if (result.Status == ItemRepositoryCommitStatus.InternalError)
                {
                    throw new InvalidOperationException(
                        "The atomic item batch returned InternalError.");
                }

                return new DungeonRewardCommitResult(
                    false,
                    $"Item settlement failed with {result.Status} for actor '{result.Actor.OwnerId}'.");
            }

            await transaction.CommitAsync(cancellationToken);
            return new DungeonRewardCommitResult(true, null);
        }
        catch (PostgresException exception)
            when (exception.SqlState == PostgresErrorCodes.UniqueViolation)
        {
            await RollbackQuietlyAsync(transaction, cancellationToken);
            logger.LogWarning(
                exception,
                "Atomic dungeon reward settlement hit a uniqueness conflict.");
            return new DungeonRewardCommitResult(
                false,
                "Item settlement failed with a deterministic ID or delivery-location conflict.");
        }
        catch
        {
            await RollbackQuietlyAsync(transaction, cancellationToken);
            throw;
        }
    }

    private static async Task RollbackQuietlyAsync(
        NpgsqlTransaction transaction,
        CancellationToken cancellationToken)
    {
        try
        {
            await transaction.RollbackAsync(cancellationToken);
        }
        catch
        {
            // Preserve the original transaction failure.
        }
    }
}
