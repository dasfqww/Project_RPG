using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

public sealed class ItemTransactionService(
    IItemRepository repository,
    TimeProvider timeProvider)
{
    public async Task<ItemRepositoryCommitResult> CommitAsync(
        ItemRepositoryCommitRequest request,
        CancellationToken cancellationToken)
    {
        DateTimeOffset now = timeProvider.GetUtcNow();
        if (!ItemRecordRules.TryValidateCommit(request, out _))
        {
            return Failure(
                request,
                ItemRepositoryCommitStatus.InvalidRequest,
                now);
        }

        ItemRepositoryCommitResult? existing =
            await repository.TryGetCommitResultAsync(
                request.RequestId,
                cancellationToken);
        if (existing is not null)
        {
            return IsSameCommand(existing, request)
                ? existing with
                {
                    Status = ItemRepositoryCommitStatus.AlreadyCommitted
                }
                : Failure(
                    request,
                    ItemRepositoryCommitStatus.IdempotencyConflict,
                    now);
        }

        ItemRepositoryCommitResult result = await repository.CommitAsync(
            request,
            now,
            cancellationToken);
        if (result.Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && !IsSameCommand(result, request))
        {
            return Failure(
                request,
                ItemRepositoryCommitStatus.IdempotencyConflict,
                now);
        }

        return result;
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

    private static ItemRepositoryCommitResult Failure(
        ItemRepositoryCommitRequest request,
        ItemRepositoryCommitStatus status,
        DateTimeOffset now)
    {
        return new ItemRepositoryCommitResult(
            status,
            request.RequestId,
            request.Operation,
            request.CommandFingerprint,
            request.Actor,
            request.AffectedQuantity,
            [],
            now);
    }
}
