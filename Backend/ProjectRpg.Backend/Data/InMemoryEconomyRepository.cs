using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryEconomyRepository(
    InMemoryTransactionGate transactionGate) : IEconomyRepository
{
    private readonly Dictionary<string, CurrencyDefinition> _definitions =
        new(StringComparer.Ordinal);
    private readonly Dictionary<(CurrencyScope, string, string), CurrencyBalance>
        _balances = [];
    private readonly Dictionary<Guid, CurrencyTransactionResult> _receipts = [];
    private readonly object _gate = transactionGate.SyncRoot;

    internal sealed record Snapshot(
        Dictionary<(CurrencyScope, string, string), CurrencyBalance> Balances,
        Dictionary<Guid, CurrencyTransactionResult> Receipts);

    internal Snapshot CaptureSnapshot()
    {
        lock (_gate)
        {
            return new Snapshot(new(_balances), new(_receipts));
        }
    }

    internal void RestoreSnapshot(Snapshot snapshot)
    {
        lock (_gate)
        {
            _balances.Clear();
            foreach (var entry in snapshot.Balances)
            {
                _balances.Add(entry.Key, entry.Value);
            }

            _receipts.Clear();
            foreach (var entry in snapshot.Receipts)
            {
                _receipts.Add(entry.Key, entry.Value);
            }
        }
    }

    public Task UpsertDefinitionAsync(
        CurrencyDefinition definition,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_definitions.TryGetValue(
                definition.CurrencyCode,
                out CurrencyDefinition? existing)
                && existing.Scope != definition.Scope)
            {
                throw new InvalidOperationException(
                    "A currency definition scope cannot change after creation.");
            }

            long highestStoredBalance = _balances
                .Where(entry => string.Equals(
                    entry.Key.Item3,
                    definition.CurrencyCode,
                    StringComparison.Ordinal))
                .Select(entry => entry.Value.Balance)
                .DefaultIfEmpty(0)
                .Max();
            if (highestStoredBalance > definition.MaxBalance)
            {
                throw new InvalidOperationException(
                    $"MaxBalance cannot be lower than the existing balance " +
                    $"'{highestStoredBalance}'.");
            }

            _definitions[definition.CurrencyCode] = definition;
        }

        return Task.CompletedTask;
    }

    public Task<IReadOnlyList<CurrencyDefinition>> GetDefinitionsAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            IReadOnlyList<CurrencyDefinition> definitions = _definitions.Values
                .OrderBy(definition => definition.CurrencyCode, StringComparer.Ordinal)
                .ToArray();
            return Task.FromResult(definitions);
        }
    }

    public Task<CurrencyWallet> LoadWalletAsync(
        CharacterEconomyContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            CurrencyBalance[] balances = _definitions.Values
                .Where(definition => definition.Enabled)
                .OrderBy(definition => definition.CurrencyCode, StringComparer.Ordinal)
                .Select(definition =>
                {
                    CurrencyOwnerRef owner = new(
                        definition.Scope,
                        EconomyRules.ResolveOwnerId(definition.Scope, context));
                    return _balances.GetValueOrDefault(Key(owner, definition.CurrencyCode))
                        ?? new CurrencyBalance(definition, owner, 0, 0);
                })
                .ToArray();
            return Task.FromResult(new CurrencyWallet(
                context.CharacterId,
                context.RosterId,
                context.SteamId,
                balances));
        }
    }

    public Task<CurrencyTransactionResult?> TryGetTransactionResultAsync(
        Guid requestId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            return Task.FromResult(_receipts.GetValueOrDefault(requestId));
        }
    }

    public Task<CurrencyTransactionResult> CommitAsync(
        CurrencyTransactionRequest request,
        CharacterEconomyContext context,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_receipts.TryGetValue(
                request.RequestId,
                out CurrencyTransactionResult? receipt))
            {
                return Task.FromResult(receipt with
                {
                    Status = CurrencyTransactionStatus.AlreadyCommitted
                });
            }

            if (!EconomyRules.TryValidateTransaction(request, out _))
            {
                return Task.FromResult(Failure(
                    request,
                    CurrencyTransactionStatus.InvalidRequest,
                    committedAt));
            }

            List<(CurrencyOwnerRef Owner, CurrencyDefinition Definition,
                CurrencyChange Change, long Previous, long Next, long Revision)>
                candidates = [];
            foreach (CurrencyChange change in request.Changes)
            {
                if (!_definitions.TryGetValue(
                    change.CurrencyCode,
                    out CurrencyDefinition? definition))
                {
                    return Task.FromResult(Failure(
                        request,
                        CurrencyTransactionStatus.DefinitionNotFound,
                        committedAt));
                }

                if (!definition.Enabled)
                {
                    return Task.FromResult(Failure(
                        request,
                        CurrencyTransactionStatus.CurrencyDisabled,
                        committedAt));
                }

                CurrencyOwnerRef owner = new(
                    definition.Scope,
                    EconomyRules.ResolveOwnerId(definition.Scope, context));
                CurrencyBalance? stored = _balances.GetValueOrDefault(
                    Key(owner, definition.CurrencyCode));
                long previous = stored?.Balance ?? 0;
                long next;
                try
                {
                    next = checked(previous + change.Delta);
                }
                catch (OverflowException)
                {
                    return Task.FromResult(Failure(
                        request,
                        CurrencyTransactionStatus.BalanceLimitExceeded,
                        committedAt));
                }

                if (next < 0)
                {
                    return Task.FromResult(Failure(
                        request,
                        CurrencyTransactionStatus.InsufficientBalance,
                        committedAt));
                }

                if (next > definition.MaxBalance && next >= previous)
                {
                    return Task.FromResult(Failure(
                        request,
                        CurrencyTransactionStatus.BalanceLimitExceeded,
                        committedAt));
                }

                candidates.Add((
                    owner,
                    definition,
                    change,
                    previous,
                    next,
                    (stored?.Revision ?? 0) + 1));
            }

            CurrencyChangeResult[] changes = candidates
                .Select(candidate => new CurrencyChangeResult(
                    candidate.Definition.CurrencyCode,
                    candidate.Definition.Scope,
                    candidate.Owner.OwnerId,
                    candidate.Change.Delta,
                    candidate.Previous,
                    candidate.Next,
                    candidate.Revision))
                .ToArray();
            foreach (var candidate in candidates)
            {
                _balances[Key(candidate.Owner, candidate.Definition.CurrencyCode)] =
                    new CurrencyBalance(
                        candidate.Definition,
                        candidate.Owner,
                        candidate.Next,
                        candidate.Revision);
            }

            CurrencyTransactionResult result = new(
                CurrencyTransactionStatus.Committed,
                request.RequestId,
                request.CharacterId,
                request.Operation,
                request.CommandFingerprint,
                request.Reason,
                changes,
                committedAt);
            _receipts.Add(request.RequestId, result);
            return Task.FromResult(result);
        }
    }

    public Task<IReadOnlyList<CurrencyTransactionResult>> CommitBatchAsync(
        IReadOnlyList<CurrencyBatchEntry> entries,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            Dictionary<(CurrencyScope, string, string), CurrencyBalance>
                workingBalances = new(_balances);
            List<CurrencyTransactionResult> results = [];
            List<CurrencyTransactionResult> newReceipts = [];

            foreach (CurrencyBatchEntry entry in entries
                .OrderBy(value => value.Request.RequestId))
            {
                CurrencyTransactionRequest request = entry.Request;
                if (_receipts.TryGetValue(
                    request.RequestId,
                    out CurrencyTransactionResult? receipt))
                {
                    if (!IsSameCommand(receipt, request))
                    {
                        return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                            [Failure(
                                request,
                                CurrencyTransactionStatus.IdempotencyConflict,
                                committedAt)]);
                    }

                    results.Add(receipt with
                    {
                        Status = CurrencyTransactionStatus.AlreadyCommitted
                    });
                    continue;
                }

                if (!EconomyRules.TryValidateTransaction(request, out _))
                {
                    return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                        [Failure(
                            request,
                            CurrencyTransactionStatus.InvalidRequest,
                            committedAt)]);
                }

                List<CurrencyBalance> updates = [];
                List<CurrencyChangeResult> changeResults = [];
                foreach (CurrencyChange change in request.Changes
                    .OrderBy(value => value.CurrencyCode, StringComparer.Ordinal))
                {
                    if (!_definitions.TryGetValue(
                        change.CurrencyCode,
                        out CurrencyDefinition? definition))
                    {
                        return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                            [Failure(
                                request,
                                CurrencyTransactionStatus.DefinitionNotFound,
                                committedAt)]);
                    }

                    if (!definition.Enabled)
                    {
                        return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                            [Failure(
                                request,
                                CurrencyTransactionStatus.CurrencyDisabled,
                                committedAt)]);
                    }

                    CurrencyOwnerRef owner = new(
                        definition.Scope,
                        EconomyRules.ResolveOwnerId(
                            definition.Scope,
                            entry.Context));
                    CurrencyBalance? stored = workingBalances.GetValueOrDefault(
                        Key(owner, definition.CurrencyCode));
                    long previous = stored?.Balance ?? 0;
                    long next;
                    try
                    {
                        next = checked(previous + change.Delta);
                    }
                    catch (OverflowException)
                    {
                        return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                            [Failure(
                                request,
                                CurrencyTransactionStatus.BalanceLimitExceeded,
                                committedAt)]);
                    }

                    if (next < 0)
                    {
                        return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                            [Failure(
                                request,
                                CurrencyTransactionStatus.InsufficientBalance,
                                committedAt)]);
                    }

                    if (next > definition.MaxBalance && next >= previous)
                    {
                        return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                            [Failure(
                                request,
                                CurrencyTransactionStatus.BalanceLimitExceeded,
                                committedAt)]);
                    }

                    long revision = (stored?.Revision ?? 0) + 1;
                    updates.Add(new CurrencyBalance(
                        definition,
                        owner,
                        next,
                        revision));
                    changeResults.Add(new CurrencyChangeResult(
                        definition.CurrencyCode,
                        definition.Scope,
                        owner.OwnerId,
                        change.Delta,
                        previous,
                        next,
                        revision));
                }

                foreach (CurrencyBalance update in updates)
                {
                    workingBalances[Key(
                        update.Owner,
                        update.Definition.CurrencyCode)] = update;
                }

                CurrencyTransactionResult result = new(
                    CurrencyTransactionStatus.Committed,
                    request.RequestId,
                    request.CharacterId,
                    request.Operation,
                    request.CommandFingerprint,
                    request.Reason,
                    changeResults,
                    committedAt);
                results.Add(result);
                newReceipts.Add(result);
            }

            foreach ((var key, CurrencyBalance value) in workingBalances)
            {
                _balances[key] = value;
            }
            foreach (CurrencyTransactionResult receipt in newReceipts)
            {
                _receipts[receipt.RequestId] = receipt;
            }

            return Task.FromResult<IReadOnlyList<CurrencyTransactionResult>>(
                results);
        }
    }

    private static bool IsSameCommand(
        CurrencyTransactionResult result,
        CurrencyTransactionRequest request)
    {
        if (result.CharacterId != request.CharacterId
            || !string.Equals(result.Operation, request.Operation, StringComparison.Ordinal)
            || !string.Equals(
                result.CommandFingerprint,
                request.CommandFingerprint,
                StringComparison.Ordinal)
            || !string.Equals(result.Reason, request.Reason, StringComparison.Ordinal)
            || result.Changes.Count != request.Changes.Count)
        {
            return false;
        }

        Dictionary<string, long> requestedChanges = request.Changes
            .ToDictionary(
                change => change.CurrencyCode,
                change => change.Delta,
                StringComparer.Ordinal);
        return result.Changes.All(change =>
            requestedChanges.TryGetValue(change.CurrencyCode, out long delta)
            && delta == change.Delta);
    }

    private static (CurrencyScope, string, string) Key(
        CurrencyOwnerRef owner,
        string currencyCode)
    {
        return (owner.Scope, owner.OwnerId, currencyCode);
    }

    private static CurrencyTransactionResult Failure(
        CurrencyTransactionRequest request,
        CurrencyTransactionStatus status,
        DateTimeOffset committedAt)
    {
        return new CurrencyTransactionResult(
            status,
            request.RequestId,
            request.CharacterId,
            request.Operation,
            request.CommandFingerprint,
            request.Reason,
            [],
            committedAt);
    }
}
