using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryItemRepository(
    InMemoryTransactionGate transactionGate) : IItemRepository
{
    private readonly Dictionary<Guid, ItemRecord> _records = [];
    private readonly Dictionary<Guid, ItemRepositoryCommitResult> _receipts = [];
    private readonly object _gate = transactionGate.SyncRoot;

    internal sealed record Snapshot(
        Dictionary<Guid, ItemRecord> Records,
        Dictionary<Guid, ItemRepositoryCommitResult> Receipts);

    internal Snapshot CaptureSnapshot()
    {
        lock (_gate)
        {
            return new Snapshot(new(_records), new(_receipts));
        }
    }

    internal void RestoreSnapshot(Snapshot snapshot)
    {
        lock (_gate)
        {
            _records.Clear();
            foreach (var entry in snapshot.Records)
            {
                _records.Add(entry.Key, entry.Value);
            }

            _receipts.Clear();
            foreach (var entry in snapshot.Receipts)
            {
                _receipts.Add(entry.Key, entry.Value);
            }
        }
    }

    public Task<ItemRecord?> FindAsync(
        Guid itemId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            return Task.FromResult(
                _records.GetValueOrDefault(itemId));
        }
    }

    public Task<IReadOnlyList<ItemRecord>> FindByOwnerAsync(
        ItemOwnerRef owner,
        bool includeTerminal,
        int limit,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            ItemRecord[] records = _records.Values
                .Where(record =>
                    record.Owner == owner
                    && (includeTerminal
                        || record.LifecycleState == ItemLifecycleState.Active))
                .OrderBy(record => record.Location.ContainerType)
                .ThenBy(record => record.Location.ContainerId, StringComparer.Ordinal)
                .ThenBy(record => record.Location.SlotIndex)
                .ThenBy(record => record.ItemId)
                .Take(limit)
                .ToArray();
            return Task.FromResult<IReadOnlyList<ItemRecord>>(records);
        }
    }

    public Task<ItemRepositoryCommitResult?> TryGetCommitResultAsync(
        Guid requestId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            return Task.FromResult(
                _receipts.GetValueOrDefault(requestId));
        }
    }

    public Task<ItemRepositoryCommitResult> CommitAsync(
        ItemRepositoryCommitRequest request,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_receipts.TryGetValue(
                request.RequestId,
                out ItemRepositoryCommitResult? receipt))
            {
                return Task.FromResult(receipt with
                {
                    Status = ItemRepositoryCommitStatus.AlreadyCommitted
                });
            }

            if (!ItemRecordRules.TryValidateCommit(request, out _))
            {
                return Task.FromResult(
                    Failure(
                        request,
                        ItemRepositoryCommitStatus.InvalidRequest,
                        committedAt));
            }

            foreach (ItemRecordMutation mutation in request.Mutations)
            {
                bool exists = _records.TryGetValue(
                    mutation.NewRecord.ItemId,
                    out ItemRecord? stored);
                if (mutation.ExpectedRevision == 0)
                {
                    if (exists)
                    {
                        return Task.FromResult(
                            Failure(
                                request,
                                ItemRepositoryCommitStatus.RevisionConflict,
                                committedAt));
                    }
                }
                else if (!exists)
                {
                    return Task.FromResult(
                        Failure(
                            request,
                            ItemRepositoryCommitStatus.NotFound,
                            committedAt));
                }
                else if (stored!.Revision != mutation.ExpectedRevision)
                {
                    return Task.FromResult(
                        Failure(
                            request,
                            ItemRepositoryCommitStatus.RevisionConflict,
                            committedAt));
                }
            }

            Dictionary<Guid, ItemRecord> candidates = new(_records);
            List<ItemRecord> writtenRecords = [];
            foreach (ItemRecordMutation mutation in request.Mutations)
            {
                ItemRecord written = ItemRecordRules.SnapshotWithRevision(
                    mutation.NewRecord,
                    mutation.ExpectedRevision + 1);
                if (!ItemRecordRules.TryValidateRecord(written, out _))
                {
                    return Task.FromResult(
                        Failure(
                            request,
                            ItemRepositoryCommitStatus.ValidationFailed,
                            committedAt));
                }

                candidates[written.ItemId] = written;
                writtenRecords.Add(written);
            }

            HashSet<(ItemOwnerRef Owner, ItemLocation Location)> locations = [];
            foreach (ItemRecord record in candidates.Values)
            {
                if (record.LifecycleState == ItemLifecycleState.Active
                    && !locations.Add((record.Owner, record.Location)))
                {
                    return Task.FromResult(
                        Failure(
                            request,
                            ItemRepositoryCommitStatus.LocationConflict,
                            committedAt));
                }
            }

            _records.Clear();
            foreach ((Guid itemId, ItemRecord record) in candidates)
            {
                _records.Add(itemId, record);
            }

            ItemRepositoryCommitResult result = new(
                ItemRepositoryCommitStatus.Committed,
                request.RequestId,
                request.Operation,
                request.CommandFingerprint,
                request.Actor,
                request.AffectedQuantity,
                writtenRecords,
                committedAt);
            _receipts.Add(request.RequestId, result);
            return Task.FromResult(result);
        }
    }

    private static ItemRepositoryCommitResult Failure(
        ItemRepositoryCommitRequest request,
        ItemRepositoryCommitStatus status,
        DateTimeOffset committedAt)
    {
        return new ItemRepositoryCommitResult(
            status,
            request.RequestId,
            request.Operation,
            request.CommandFingerprint,
            request.Actor,
            request.AffectedQuantity,
            [],
            committedAt);
    }
}
