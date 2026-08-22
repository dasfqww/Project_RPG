namespace ProjectRpg.Backend.Contracts;

public sealed record CurrencyDefinitionContract(
    string? CurrencyCode,
    string? DisplayName,
    string? Scope,
    long MaxBalance,
    bool Enabled);

public sealed record CurrencyChangeContract(
    string? CurrencyCode,
    long Delta);

public sealed record CurrencyTransactionRequestContract(
    Guid RequestId,
    Guid CharacterId,
    Guid DungeonSessionId,
    string? Operation,
    string? CommandFingerprint,
    string? Reason,
    IReadOnlyList<CurrencyChangeContract?>? Changes);

public sealed record CurrencyBalanceContract(
    string CurrencyCode,
    string DisplayName,
    string Scope,
    string OwnerId,
    long Balance,
    long MaxBalance,
    long Revision);

public sealed record CurrencyWalletResponse(
    Guid CharacterId,
    Guid RosterId,
    string AccountId,
    IReadOnlyList<CurrencyBalanceContract> Balances);

public sealed record CurrencyChangeResultContract(
    string CurrencyCode,
    string Scope,
    string OwnerId,
    long Delta,
    long PreviousBalance,
    long NewBalance,
    long Revision);

public sealed record CurrencyTransactionResponse(
    string Status,
    Guid RequestId,
    Guid CharacterId,
    string Operation,
    string CommandFingerprint,
    string Reason,
    IReadOnlyList<CurrencyChangeResultContract> Changes,
    DateTimeOffset CommittedAt);

public sealed record CurrencyDefinitionsResponse(
    IReadOnlyList<CurrencyDefinitionContract> Definitions);
