using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IEconomyRepository
{
    Task UpsertDefinitionAsync(
        CurrencyDefinition definition,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken);

    Task<IReadOnlyList<CurrencyDefinition>> GetDefinitionsAsync(
        CancellationToken cancellationToken);

    Task<CurrencyWallet> LoadWalletAsync(
        CharacterEconomyContext context,
        CancellationToken cancellationToken);

    Task<CurrencyTransactionResult?> TryGetTransactionResultAsync(
        Guid requestId,
        CancellationToken cancellationToken);

    Task<CurrencyTransactionResult> CommitAsync(
        CurrencyTransactionRequest request,
        CharacterEconomyContext context,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken);

    Task<IReadOnlyList<CurrencyTransactionResult>> CommitBatchAsync(
        IReadOnlyList<CurrencyBatchEntry> entries,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken);
}
