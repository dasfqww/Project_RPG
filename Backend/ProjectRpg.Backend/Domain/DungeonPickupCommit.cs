using System.Text.Json;
using ProjectRpg.Backend.Api;

namespace ProjectRpg.Backend.Domain;

public enum DungeonPickupCommitStatus
{
    Committed, AlreadyCommitted, InvalidRequest, Forbidden, NotFound,
    IdempotencyConflict, RevisionConflict, LocationConflict, ValidationFailed, InternalError
}

public sealed record DungeonPickupCommitResult(DungeonPickupCommitStatus Status,
    ItemRepositoryCommitResult? Receipt = null)
{
    internal static DungeonPickupCommitResult FromItem(ItemRepositoryCommitResult result)
    {
        var status = Enum.Parse<DungeonPickupCommitStatus>(result.Status.ToString());
        // Do not return references to the Memory repository's mutable receipt arrays.
        return new(status, result.Status is ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted
            ? result with { Records = result.Records.Select(r => ItemRecordRules.SnapshotWithRevision(r, r.Revision)).ToArray() }
            : null);
    }
}

internal static class DungeonPickupReceiptRules
{
    internal static bool Matches(ItemRepositoryCommitRequest command, ItemRepositoryCommitResult receipt)
    {
        if (receipt.Status is not (ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted)
            || receipt.RequestId != command.RequestId || receipt.Operation != command.Operation
            || receipt.CommandFingerprint != command.CommandFingerprint || receipt.Actor != command.Actor
            || receipt.AffectedQuantity != command.AffectedQuantity || receipt.Records.Count != 1) return false;
        var expected = ItemRecordRules.SnapshotWithRevision(command.Mutations[0].NewRecord, 1);
        return JsonSerializer.Serialize(ItemApiContractMapper.ToContract(expected), DungeonPickupIntentRules.JsonOptions)
            == JsonSerializer.Serialize(ItemApiContractMapper.ToContract(receipt.Records[0]), DungeonPickupIntentRules.JsonOptions);
    }
}
