using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class EconomyApiContractMapper
{
    public static bool TryToDomain(
        CurrencyDefinitionContract contract,
        out CurrencyDefinition definition,
        out string error)
    {
        definition = default!;
        string currencyCode = contract.CurrencyCode?.Trim() ?? string.Empty;
        string displayName = contract.DisplayName?.Trim() ?? string.Empty;
        if (!Enum.TryParse(
            contract.Scope?.Trim(),
            true,
            out CurrencyScope scope))
        {
            error = "Scope must be Account, Roster, or Character.";
            return false;
        }

        definition = new CurrencyDefinition(
            currencyCode,
            displayName,
            scope,
            contract.MaxBalance,
            contract.Enabled);
        return EconomyRules.TryValidateDefinition(definition, out error);
    }

    public static bool TryToDomain(
        CurrencyTransactionRequestContract contract,
        out CurrencyTransactionRequest request,
        out string error)
    {
        request = default!;
        if (contract.Changes is null
            || contract.Changes.Any(change => change is null))
        {
            error = "Changes is required and cannot contain null entries.";
            return false;
        }

        CurrencyChange[] changes = contract.Changes
            .Select(change => new CurrencyChange(
                change!.CurrencyCode?.Trim() ?? string.Empty,
                change.Delta))
            .ToArray();
        request = new CurrencyTransactionRequest(
            contract.RequestId,
            contract.CharacterId,
            contract.DungeonSessionId,
            contract.Operation?.Trim() ?? string.Empty,
            contract.CommandFingerprint?.Trim() ?? string.Empty,
            contract.Reason?.Trim() ?? string.Empty,
            changes);
        return EconomyRules.TryValidateTransaction(request, out error);
    }

    public static CurrencyDefinitionContract ToContract(
        CurrencyDefinition definition)
    {
        return new CurrencyDefinitionContract(
            definition.CurrencyCode,
            definition.DisplayName,
            definition.Scope.ToString(),
            definition.MaxBalance,
            definition.Enabled);
    }

    public static CurrencyWalletResponse ToContract(CurrencyWallet wallet)
    {
        return new CurrencyWalletResponse(
            wallet.CharacterId,
            wallet.RosterId,
            wallet.AccountId,
            wallet.Balances.Select(balance => new CurrencyBalanceContract(
                balance.Definition.CurrencyCode,
                balance.Definition.DisplayName,
                balance.Definition.Scope.ToString(),
                balance.Owner.OwnerId,
                balance.Balance,
                balance.Definition.MaxBalance,
                balance.Revision)).ToArray());
    }

    public static CurrencyTransactionResponse ToContract(
        CurrencyTransactionResult result)
    {
        return new CurrencyTransactionResponse(
            result.Status.ToString(),
            result.RequestId,
            result.CharacterId,
            result.Operation,
            result.CommandFingerprint,
            result.Reason,
            result.Changes.Select(change => new CurrencyChangeResultContract(
                change.CurrencyCode,
                change.Scope.ToString(),
                change.OwnerId,
                change.Delta,
                change.PreviousBalance,
                change.NewBalance,
                change.Revision)).ToArray(),
            result.CommittedAt);
    }
}
