using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

public sealed class ItemTransactionService(
    IItemRepository repository,
    TimeProvider timeProvider,
    IGameTransactionObserver? observer = null,
    IGameStorageObserver? storageObserver = null)
{
    public async Task<ItemRepositoryCommitResult> CommitAsync(
        ItemRepositoryCommitRequest request,
        CancellationToken cancellationToken)
    {
        var result = await CommitCoreAsync(request, cancellationToken);
        if (observer is not null && result.Status is ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted)
        {
            try
            {
                observer.Observe(new(GameTransactionKind.Item,
                    result.Status == ItemRepositoryCommitStatus.Committed ? GameTransactionDisposition.Committed : GameTransactionDisposition.Replayed,
                    result.RequestId));
            }
            catch (Exception) { /* Technical log failure must not change a confirmed receipt. */ }
        }
        return result;
    }

    private async Task<ItemRepositoryCommitResult> CommitCoreAsync(
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
            await GameStorageCall.RunAsync(storageObserver, GameStorageOperation.ItemReceiptRead, request.RequestId,
                () => repository.TryGetCommitResultAsync(
                request.RequestId,
                cancellationToken));
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

        ItemRepositoryCommitResult result = await GameStorageCall.RunAsync(storageObserver, GameStorageOperation.ItemCommit,
            request.RequestId, () => repository.CommitAsync(
            request,
            now,
            cancellationToken));
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
        if (result.Status is not (ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted)
            || result.RequestId != request.RequestId
            || !string.Equals(result.Operation, request.Operation, StringComparison.Ordinal)
            || !string.Equals(result.CommandFingerprint, request.CommandFingerprint, StringComparison.Ordinal)
            || result.Actor != request.Actor
            || result.AffectedQuantity != request.AffectedQuantity
            || result.Records.Count != request.Mutations.Count)
        {
            return false;
        }

        // The declared fingerprint is not proof of an unchanged command. Compare
        // the original receipt, never today's item state (which may have moved on).
        for (int index = 0; index < request.Mutations.Count; index++)
        {
            ItemRecordMutation mutation = request.Mutations[index];
            ItemRecord expected = mutation.NewRecord;
            ItemRecord actual = result.Records[index];
            if (mutation.ExpectedRevision == long.MaxValue
                || actual.Revision != mutation.ExpectedRevision + 1
                || actual.DefinitionType != expected.DefinitionType
                || actual.DefinitionName != expected.DefinitionName
                || actual.DefinitionVersion != expected.DefinitionVersion
                || actual.Owner != expected.Owner
                || actual.Location != expected.Location
                || actual.LifecycleState != expected.LifecycleState
                || actual.Metadata != expected.Metadata
                || actual.State.InstanceId != expected.State.InstanceId
                || actual.State.GenerationSeed != expected.State.GenerationSeed
                || actual.State.Quantity != expected.State.Quantity
                || actual.State.EquipmentProgression != expected.State.EquipmentProgression
                || !actual.State.InstanceTags.SequenceEqual(expected.State.InstanceTags, StringComparer.Ordinal)
                || !actual.State.StatValues.SequenceEqual(expected.State.StatValues))
            {
                return false;
            }
        }

        return true;
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
