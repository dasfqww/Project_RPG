using ProjectRpg.Backend.Domain;
using Trading = ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryItemRepository(
    InMemoryTransactionGate transactionGate) : IItemRepository
{
    private readonly Dictionary<Guid, ItemRecord> _records = [];
    private readonly Dictionary<Guid, ItemRepositoryCommitResult> _receipts = [];
    private readonly object _gate = transactionGate.SyncRoot;

    internal object TransactionSyncRoot => _gate;

    internal IReadOnlyList<ItemRecord> TradeInventory(ItemOwnerRef owner) => _records.Values.Where(item =>
        item.Owner == owner && item.LifecycleState == ItemLifecycleState.Active
        && item.Location.ContainerType == ItemContainerType.Inventory).ToArray();

    // Only the transaction coordinator may bypass reservation guards, after validating both live holds and the plan.
    internal void ApplyTradePlan(IReadOnlyList<ItemRecordMutation> mutations)
    {
        if (!Monitor.IsEntered(_gate)) throw new InvalidOperationException("Asset transaction gate required.");
        if (mutations.Count > Trading.TradeExchangePlanner.MaximumMutationCount) throw new InvalidDataException("Trade mutation limit");
        Dictionary<Guid, ItemRecord> next = new(_records);
        HashSet<Guid> seen = [];
        foreach (var m in mutations)
        {
            if (!seen.Add(m.NewRecord.ItemId) || m.ExpectedRevision < 0 || m.ExpectedRevision == long.MaxValue
                || m.NewRecord.Revision != m.ExpectedRevision
                || (m.ExpectedRevision == 0 ? _records.ContainsKey(m.NewRecord.ItemId)
                    : !_records.TryGetValue(m.NewRecord.ItemId, out var old) || old.Revision != m.ExpectedRevision))
                throw new Trading.TradeRuleException("trade_item_revision_conflict");
            var item = ItemRecordRules.SnapshotWithRevision(m.NewRecord, m.ExpectedRevision + 1);
            if (!ItemRecordRules.TryValidateRecord(item, out _)) throw new InvalidDataException("Invalid trade item");
            next[item.ItemId] = item;
        }
        if (next.Values.Where(i => i.LifecycleState == ItemLifecycleState.Active)
            .GroupBy(i => (i.Owner, i.Location)).Any(group => group.Count() > 1))
            throw new Trading.TradeRuleException("trade_inventory_full");
        foreach (var m in mutations) _records[m.NewRecord.ItemId] = next[m.NewRecord.ItemId];
    }

    internal void SeedStarter(ItemRecord record) => SeedStarters([record]);

    internal void SeedStarters(IReadOnlyList<ItemRecord> records)
    {
        if (records.Count == 0) throw new InvalidOperationException("A starter batch cannot be empty.");
        ItemRecord[] pending = records.Select(record => ItemRecordRules.SnapshotWithRevision(record, record.Revision)).ToArray();
        HashSet<Guid> identities = [];
        HashSet<(ItemOwnerRef Owner, ItemLocation Location)> locations = [];
        foreach (ItemRecord item in pending)
        {
            if (!ItemRecordRules.TryValidateRecord(item, out string error) || item.Revision != 1
                || item.Metadata.CreationSource != "CharacterCreation" || item.LifecycleState != ItemLifecycleState.Active
                || item.Owner != pending[0].Owner || !identities.Add(item.ItemId) || !locations.Add((item.Owner, item.Location)))
                throw new InvalidOperationException("Invalid starter equipment batch: " + error);
        }
        lock (_gate)
        {
            // Check the ENTIRE batch before publishing any item. Character creation
            // holds this same gate and the character gate until all records exist.
            foreach (ItemRecord item in pending)
            {
                if (_records.ContainsKey(item.ItemId) || _records.Values.Any(existing =>
                    existing.LifecycleState == ItemLifecycleState.Active && existing.Owner == item.Owner
                    && existing.Location == item.Location))
                    throw new InvalidOperationException("Starter equipment identity or slot already exists.");
            }
            foreach (ItemRecord item in pending) _records.Add(item.ItemId, item);
        }
    }

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
        => CommitCore(request, committedAt, cancellationToken, isReward: false);

    internal Task<ItemRepositoryCommitResult> CommitRewardAsync(
        ItemRepositoryCommitRequest request,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
        => CommitCore(request, committedAt, cancellationToken, isReward: true);

    internal IReadOnlyList<ItemRecord> FindInventory(ItemOwnerRef owner)
    {
        lock (_gate)
        {
            return _records.Values.Where(record =>
                DungeonRewardDeliveryPlanner.IsInventoryRecord(record, owner)).ToArray();
        }
    }

    internal Task<ItemRepositoryCommitResult> CommitRewardGrantAsync(
        ItemRepositoryCommitRequest grant,
        IReadOnlyList<int>? maxStackSizes,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_receipts.TryGetValue(grant.RequestId, out ItemRepositoryCommitResult? receipt))
            {
                return Task.FromResult(receipt with { Status = ItemRepositoryCommitStatus.AlreadyCommitted });
            }
            // Even a fully merged reward must not hide a deterministic grant-ID collision.
            if (grant.Mutations.Any(mutation => _records.ContainsKey(mutation.NewRecord.ItemId)))
            {
                return Task.FromResult(Failure(grant, ItemRepositoryCommitStatus.RevisionConflict, committedAt));
            }
            ItemRepositoryCommitRequest planned = DungeonRewardDeliveryPlanner.Plan(grant, maxStackSizes, FindInventory(grant.Actor),
                FindInventory(grant.Actor).Where(item => transactionGate.IsItemReserved(item.ItemId)).Select(item => item.ItemId).ToHashSet());
            return CommitRewardAsync(planned, committedAt, cancellationToken);
        }
    }

    private Task<ItemRepositoryCommitResult> CommitCore(
        ItemRepositoryCommitRequest request,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken,
        bool isReward)
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

            if (!(isReward
                ? ItemRecordRules.TryValidateRewardCommit(request, out _)
                : ItemRecordRules.TryValidateCommit(request, out _)))
            {
                return Task.FromResult(
                    Failure(
                        request,
                        ItemRepositoryCommitStatus.InvalidRequest,
                        committedAt));
            }

            if (!InventoryBagLayout.ValidNewDestinations(request))
                return Task.FromResult(Failure(request, ItemRepositoryCommitStatus.ValidationFailed, committedAt));

            if (request.Mutations.Any(mutation => transactionGate.IsItemReserved(mutation.NewRecord.ItemId)
                || _records.TryGetValue(mutation.NewRecord.ItemId, out var held)
                    && (held.Location.ContainerType == ItemContainerType.Auction || Trading.AuctionEscrowRules.IsOwner(held.Owner))))
                return Task.FromResult(Failure(request, ItemRepositoryCommitStatus.ValidationFailed, committedAt));

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

            if (request.Operation == InventoryBagLayout.SortOperation &&
                (request.AffectedQuantity != 0 || request.Mutations.Select(m => InventoryBagLayout.Tab(m.NewRecord.DefinitionName)).Distinct().Count() != 1 ||
                 request.Mutations.Any(m => m.NewRecord.Owner != request.Actor || !InventoryBagLayout.IsSortMutation(_records.GetValueOrDefault(m.NewRecord.ItemId), m.NewRecord))))
                return Task.FromResult(Failure(request, ItemRepositoryCommitStatus.ValidationFailed, committedAt));

            // An old client must not erase a stored growth value by omitting the new field.
            foreach (ItemRecordMutation mutation in request.Mutations)
            {
                if (_records.TryGetValue(mutation.NewRecord.ItemId, out ItemRecord? existing)
                    && existing.State.EquipmentProgression is not null
                    && mutation.NewRecord.State.EquipmentProgression is null)
                    return Task.FromResult(Failure(request, ItemRepositoryCommitStatus.ValidationFailed, committedAt));
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
