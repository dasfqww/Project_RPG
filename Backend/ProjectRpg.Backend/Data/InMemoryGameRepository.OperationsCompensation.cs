using ProjectRpg.Backend.Operations.Infrastructure.Compensation;
using ProjectRpg.Backend.Operations.Infrastructure.Reads;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Compensation;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    private sealed record StoredAction(string Hash, OperationAction Receipt);
    private readonly Dictionary<Guid, StoredAction> _operationsActions = [];
    private readonly Dictionary<(Guid Case, Guid Character), Guid> _operationsCases = [];

    internal Result<ActionCommit> CommitOperationsCompensation(CompensationCommand command, ICompensationPolicy policy,
        DateTimeOffset now, CancellationToken ct)
    {
        InMemoryItemRepository items = _starterItems ?? throw new InvalidOperationException("Shared item gate required.");
        // Same dungeon -> item -> character ordering as equipment/pickup. No awaits under Monitor.
        lock (_dungeonGate)
        lock (items.TransactionSyncRoot)
        lock (_characterGate)
        {
            ct.ThrowIfCancellationRequested();
            var itemReceipt = items.TryGetCommitResultAsync(command.RequestId, ct).GetAwaiter().GetResult();
            if (_operationsActions.TryGetValue(command.RequestId, out var stored))
            {
                if (!CompensationCatalog.Consistent(stored.Receipt, stored.Hash, itemReceipt)) return Fail(CompensationFailures.Inconsistent);
                return stored.Hash != command.InputHash ? Fail(CompensationFailures.RequestConflict)
                    : Result<ActionCommit>.Success(new("Replayed", stored.Receipt));
            }
            if (itemReceipt is not null) return Fail(CompensationFailures.Inconsistent);
            if (CompensationFailures.CheckNew(command, policy, now) is { } policyError) return Fail(policyError);
            var intent = command.Preview.Intent;
            if (!_characters.ContainsKey(intent.CharacterId)) return Fail(Failures.CharacterMissing);
            if (_operationsCases.ContainsKey((intent.CaseId, intent.CharacterId))) return Fail(CompensationFailures.CaseConflict);
            if ((_characterLeases.TryGetValue(intent.CharacterId, out var lease) && lease.ExpiresAt > now)
                || _dungeonSessions.Values.Any(s => s.Members.Any(m => m.CharacterId == intent.CharacterId)
                    && s.State is DungeonSessionState.Waiting or DungeonSessionState.Loading or DungeonSessionState.InProgress or DungeonSessionState.SettlementPending)
                || _dungeonRewardSettlements.Values.Any(s => s.CharacterIds.Contains(intent.CharacterId)
                    && s.State is DungeonRewardSettlementState.Pending or DungeonRewardSettlementState.Processing))
                return Fail(CompensationFailures.Busy);
            var snapshot = items.CaptureSnapshot();
            try
            {
                var item = items.CommitAsync(CompensationCatalog.Create(command), now, ct).GetAwaiter().GetResult();
                var action = CompensationCatalog.Receipt(command, item);
                ct.ThrowIfCancellationRequested();
                _operationsActions.Add(command.RequestId, new(command.InputHash, action));
                _operationsCases.Add((intent.CaseId, intent.CharacterId), command.RequestId);
                return Result<ActionCommit>.Success(new("Committed", action));
            }
            catch
            {
                items.RestoreSnapshot(snapshot);
                _operationsActions.Remove(command.RequestId);
                _operationsCases.Remove((intent.CaseId, intent.CharacterId));
                throw;
            }
        }
    }
    private static Result<ActionCommit> Fail(Failure error) => Result<ActionCommit>.Fail(error);
    internal Result<OperationAction> FindOperationsAction(Guid requestId, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            if (!_operationsActions.TryGetValue(requestId, out var action)) return Result<OperationAction>.Fail(CompensationFailures.Missing);
            var item = _starterItems!.TryGetCommitResultAsync(requestId, ct).GetAwaiter().GetResult();
            return CompensationCatalog.Consistent(action.Receipt, action.Hash, item)
                ? Result<OperationAction>.Success(action.Receipt) : Result<OperationAction>.Fail(CompensationFailures.Inconsistent);
        }
    }
    internal Result<Page<OperationAction>> SearchOperationsActions(Guid characterId, PageRequest page, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            var rows = _operationsActions.Values.Select(a => a.Receipt).Where(a => a.Target.Id == characterId)
                .Where(a => page.After is null || a.CommittedAtUtc < page.After.Timestamp
                    || (a.CommittedAtUtc == page.After.Timestamp && a.RequestId.CompareTo(page.After.Id) < 0))
                .OrderByDescending(a => a.CommittedAtUtc).ThenByDescending(a => a.RequestId).Take(page.Limit + 1).ToArray();
            foreach (var row in rows)
                if (!FindOperationsAction(row.RequestId, ct).IsSuccess) return Result<Page<OperationAction>>.Fail(CompensationFailures.Inconsistent);
            return Result<Page<OperationAction>>.Success(ReadMapping.Page(rows, page.Limit, a => new(a.RequestId, a.CommittedAtUtc)));
        }
    }
}
