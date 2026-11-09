using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

public sealed class CurrencyTransactionService(
    ICurrencyAuthorityRepository gameRepository,
    IEconomyRepository economyRepository,
    TimeProvider timeProvider,
    IGameTransactionObserver? observer = null,
    IGameStorageObserver? storageObserver = null)
{
    public async Task<CurrencyTransactionResult> CommitAsync(
        CurrencyTransactionRequest request,
        string serverId,
        CancellationToken cancellationToken)
    {
        var result = await CommitCoreAsync(request, serverId, cancellationToken);
        if (observer is not null && result.Status is CurrencyTransactionStatus.Committed or CurrencyTransactionStatus.AlreadyCommitted)
        {
            try
            {
                observer.Observe(new(GameTransactionKind.Currency,
                    result.Status == CurrencyTransactionStatus.Committed ? GameTransactionDisposition.Committed : GameTransactionDisposition.Replayed,
                    result.RequestId, result.CharacterId));
            }
            catch (Exception) { /* Technical log failure must not change a confirmed receipt. */ }
        }
        return result;
    }

    private async Task<CurrencyTransactionResult> CommitCoreAsync(
        CurrencyTransactionRequest request,
        string serverId,
        CancellationToken cancellationToken)
    {
        DateTimeOffset now = timeProvider.GetUtcNow();
        if (!EconomyRules.TryValidateTransaction(request, out _))
        {
            return Failure(
                request,
                CurrencyTransactionStatus.InvalidRequest,
                now);
        }

        CharacterEconomyContext? context =
            await gameRepository.GetCharacterEconomyContextAsync(
                request.CharacterId,
                cancellationToken);
        if (context is null)
        {
            return Failure(
                request,
                CurrencyTransactionStatus.CharacterNotFound,
                now);
        }

        bool isAuthorized =
            await gameRepository.IsAuthorizedGameServerSessionMemberAsync(
                request.DungeonSessionId,
                serverId,
                context.SteamId,
                request.CharacterId,
                now,
                cancellationToken);
        if (!isAuthorized)
        {
            return Failure(
                request,
                CurrencyTransactionStatus.SessionNotAuthorized,
                now);
        }

        CurrencyTransactionResult? existing =
            await GameStorageCall.RunAsync(storageObserver, GameStorageOperation.CurrencyReceiptRead, request.RequestId,
                () => economyRepository.TryGetTransactionResultAsync(
                request.RequestId,
                cancellationToken));
        if (existing is not null)
        {
            return IsSameCommand(existing, request)
                ? existing with
                {
                    Status = CurrencyTransactionStatus.AlreadyCommitted
                }
                : Failure(
                    request,
                    CurrencyTransactionStatus.IdempotencyConflict,
                    now);
        }

        CurrencyTransactionResult result = await GameStorageCall.RunAsync(storageObserver, GameStorageOperation.CurrencyCommit,
            request.RequestId, () => economyRepository.CommitAsync(
            request,
            context,
            now,
            cancellationToken));
        if (result.Status == CurrencyTransactionStatus.AlreadyCommitted
            && !IsSameCommand(result, request))
        {
            return Failure(
                request,
                CurrencyTransactionStatus.IdempotencyConflict,
                now);
        }

        return result;
    }

    private static bool IsSameCommand(
        CurrencyTransactionResult result,
        CurrencyTransactionRequest request)
    {
        if (result.CharacterId != request.CharacterId
            || !string.Equals(
                result.Operation,
                request.Operation,
                StringComparison.Ordinal)
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
