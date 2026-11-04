using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Wallets;

public sealed record WalletLine(string CurrencyCode, string DisplayName, string Scope, string OwnerId, long Balance, long Revision);
public sealed record WalletSnapshot(Guid CharacterId, Guid RosterId, IReadOnlyList<WalletLine> Balances)
{
    public string ScopeFilter => "ActiveDefinitions";
}
public interface IWalletReadStore
{
    Task<Result<WalletSnapshot>> ReadAsync(Guid characterId, CancellationToken cancellationToken);
}
public sealed class GetWallet(IOperatorAccess access, IWalletReadStore store)
{
    public async Task<Result<WalletSnapshot>> ExecuteAsync(Guid id, CancellationToken ct)
    {
        Result<OperatorActor> actor = await access.AuthorizeAsync(OpsCapability.ReadCharacter, ct);
        if (!actor.IsSuccess) return Result<WalletSnapshot>.Fail(actor.Error!);
        return id == Guid.Empty ? Result<WalletSnapshot>.Fail(Failures.InvalidQuery) : await store.ReadAsync(id, ct);
    }
}
