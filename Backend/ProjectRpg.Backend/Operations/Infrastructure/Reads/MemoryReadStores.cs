using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Inventory;
using ProjectRpg.Operations.Core.Sessions;
using ProjectRpg.Operations.Core.Wallets;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

public sealed class MemoryCharacterReadStore(InMemoryGameRepository game) : ICharacterReadStore
{
    public Task<Result<Page<CharacterHeader>>> SearchAsync(CharacterLookup query, CancellationToken ct)
    { ct.ThrowIfCancellationRequested(); return Task.FromResult(game.SearchOperationsCharacters(query)); }
    public Task<Result<CharacterHeader>> FindAsync(Guid id, CancellationToken ct)
    { ct.ThrowIfCancellationRequested(); return Task.FromResult(game.ReadOperationsCharacter(id)); }
}

public sealed class MemoryInventoryReadStore(InMemoryGameRepository game, InMemoryItemRepository items) : IInventoryReadStore
{
    public Task<Result<Page<ItemSummary>>> ReadAsync(Guid id, InventoryContainer container, PageRequest page, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        if (!game.ReadOperationsCharacter(id).IsSuccess) return Task.FromResult(Result<Page<ItemSummary>>.Fail(Failures.CharacterMissing));
        ItemSummary[] rows = items.CaptureSnapshot().Records.Values
            .Where(i => i.Owner == new ItemOwnerRef(ItemOwnerType.Character, id.ToString("D"))
                && i.Location.ContainerType.ToString() == container.ToString()
                && (page.After is null || i.ItemId.CompareTo(page.After.Id) > 0))
            .OrderBy(i => i.ItemId).Take(page.Limit + 1).Select(ReadMapping.Item).ToArray();
        return Task.FromResult(Result<Page<ItemSummary>>.Success(ReadMapping.Page(rows, page.Limit, i => new(i.ItemId))));
    }
}

public sealed class MemoryWalletReadStore(InMemoryGameRepository game, InMemoryEconomyRepository economy) : IWalletReadStore
{
    public async Task<Result<WalletSnapshot>> ReadAsync(Guid id, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        Result<CharacterHeader> character = game.ReadOperationsCharacter(id);
        if (!character.IsSuccess) return Result<WalletSnapshot>.Fail(character.Error!);
        CharacterHeader c = character.Value;
        CurrencyWallet wallet = await economy.LoadWalletAsync(new(id, c.RosterId, c.AccountId), ct);
        return Result<WalletSnapshot>.Success(new(id, c.RosterId, Array.AsReadOnly(wallet.Balances.Select(b =>
            new WalletLine(b.Definition.CurrencyCode, b.Definition.DisplayName, b.Owner.Scope.ToString(), b.Owner.OwnerId, b.Balance, b.Revision)).ToArray())));
    }
}

public sealed class MemorySessionEvidenceReadStore(InMemoryGameRepository game, TimeProvider clock) : ISessionEvidenceReadStore
{
    public Task<Result<Page<SessionEvidence>>> ReadAsync(Guid id, PageRequest page, CancellationToken ct)
    { ct.ThrowIfCancellationRequested(); return Task.FromResult(game.ReadOperationsSessions(id, page, clock.GetUtcNow())); }
}
