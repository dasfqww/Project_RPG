namespace ProjectRpg.Backend.Domain;

public enum CurrencyScope
{
    Account,
    Roster,
    Character
}

public enum CurrencyTransactionStatus
{
    Committed,
    AlreadyCommitted,
    InvalidRequest,
    IdempotencyConflict,
    CharacterNotFound,
    SessionNotAuthorized,
    DefinitionNotFound,
    CurrencyDisabled,
    InsufficientBalance,
    BalanceLimitExceeded,
    InternalError
}

public sealed record CurrencyDefinition(
    string CurrencyCode,
    string DisplayName,
    CurrencyScope Scope,
    long MaxBalance,
    bool Enabled);

public sealed record CurrencyOwnerRef(
    CurrencyScope Scope,
    string OwnerId);

public sealed record CurrencyBalance(
    CurrencyDefinition Definition,
    CurrencyOwnerRef Owner,
    long Balance,
    long Revision);

public sealed record CurrencyWallet(
    Guid CharacterId,
    Guid RosterId,
    string AccountId,
    IReadOnlyList<CurrencyBalance> Balances);

public sealed record CurrencyChange(
    string CurrencyCode,
    long Delta);

public sealed record CurrencyTransactionRequest(
    Guid RequestId,
    Guid CharacterId,
    Guid DungeonSessionId,
    string Operation,
    string CommandFingerprint,
    string Reason,
    IReadOnlyList<CurrencyChange> Changes);

public sealed record CurrencyBatchEntry(
    CurrencyTransactionRequest Request,
    CharacterEconomyContext Context);

public sealed record CurrencyChangeResult(
    string CurrencyCode,
    CurrencyScope Scope,
    string OwnerId,
    long Delta,
    long PreviousBalance,
    long NewBalance,
    long Revision);

public sealed record CurrencyTransactionResult(
    CurrencyTransactionStatus Status,
    Guid RequestId,
    Guid CharacterId,
    string Operation,
    string CommandFingerprint,
    string Reason,
    IReadOnlyList<CurrencyChangeResult> Changes,
    DateTimeOffset CommittedAt);

public static class EconomyRules
{
    public const int MaximumChangeCount = 16;

    public static bool TryValidateDefinition(
        CurrencyDefinition definition,
        out string error)
    {
        if (!IsSimpleIdentifier(definition.CurrencyCode, 64))
        {
            error = "CurrencyCode must contain only letters, digits, '.', '_', or '-' and be at most 64 characters.";
            return false;
        }

        if (!IsBoundedText(definition.DisplayName, 128))
        {
            error = "DisplayName is required and must be at most 128 characters.";
            return false;
        }

        if (definition.MaxBalance <= 0)
        {
            error = "MaxBalance must be positive.";
            return false;
        }

        error = string.Empty;
        return true;
    }

    public static bool TryValidateTransaction(
        CurrencyTransactionRequest request,
        out string error)
    {
        if (request.RequestId == Guid.Empty)
        {
            error = "RequestId must be a non-empty UUID.";
            return false;
        }

        if (request.CharacterId == Guid.Empty)
        {
            error = "CharacterId must be a non-empty UUID.";
            return false;
        }

        if (request.DungeonSessionId == Guid.Empty)
        {
            error = "DungeonSessionId must be a non-empty UUID.";
            return false;
        }

        if (!IsSimpleIdentifier(request.Operation, 64))
        {
            error = "Operation must be a simple identifier of at most 64 characters.";
            return false;
        }

        if (!IsBoundedText(request.CommandFingerprint, 512))
        {
            error = "CommandFingerprint is required and must be at most 512 characters.";
            return false;
        }

        if (!IsBoundedText(request.Reason, 128))
        {
            error = "Reason is required and must be at most 128 characters.";
            return false;
        }

        if (request.Changes.Count is < 1 or > MaximumChangeCount)
        {
            error = $"A transaction requires 1 to {MaximumChangeCount} currency changes.";
            return false;
        }

        HashSet<string> currencyCodes = new(StringComparer.Ordinal);
        foreach (CurrencyChange change in request.Changes)
        {
            if (!IsSimpleIdentifier(change.CurrencyCode, 64))
            {
                error = "Every CurrencyCode must be a valid simple identifier.";
                return false;
            }

            if (change.Delta == 0)
            {
                error = "Currency change Delta cannot be zero.";
                return false;
            }

            if (!currencyCodes.Add(change.CurrencyCode))
            {
                error = "A transaction cannot change the same currency more than once.";
                return false;
            }
        }

        error = string.Empty;
        return true;
    }

    public static string ResolveOwnerId(
        CurrencyScope scope,
        CharacterEconomyContext context)
    {
        return scope switch
        {
            CurrencyScope.Account => context.SteamId,
            CurrencyScope.Roster => context.RosterId.ToString("D"),
            CurrencyScope.Character => context.CharacterId.ToString("D"),
            _ => throw new ArgumentOutOfRangeException(nameof(scope))
        };
    }

    private static bool IsBoundedText(string value, int maximumLength)
    {
        return !string.IsNullOrWhiteSpace(value)
            && value.Length <= maximumLength;
    }

    public static bool IsSimpleIdentifier(string value, int maximumLength)
    {
        return IsBoundedText(value, maximumLength)
            && value.All(character =>
                char.IsAsciiLetterOrDigit(character)
                || character is '.' or '_' or '-');
    }
}
