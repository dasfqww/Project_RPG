namespace ProjectRpg.Backend.Domain;

internal static class EquipmentProgressionReceipt
{
    // The server computes this fingerprint from the entire narrow command.
    // Never accept a client-declared generic Commit fingerprint as authorization.
    internal static ItemRepositoryCommitResult Replay(ItemRepositoryCommitResult receipt, Guid requestId,
        string operation, string fingerprint, ItemOwnerRef owner)
    {
        if (receipt.RequestId != requestId || receipt.Operation != operation || receipt.Actor != owner
            || receipt.CommandFingerprint != fingerprint
            || receipt.Status is not (ItemRepositoryCommitStatus.Committed or ItemRepositoryCommitStatus.AlreadyCommitted))
            throw new EquipmentProgressionRuleException("idempotency_conflict");
        return Copy(receipt with { Status = ItemRepositoryCommitStatus.AlreadyCommitted });
    }

    internal static ItemRepositoryCommitResult Copy(ItemRepositoryCommitResult result) => result with
    {
        Records = result.Records.Select(record => ItemRecordRules.SnapshotWithRevision(record, record.Revision)).ToArray()
    };
}
