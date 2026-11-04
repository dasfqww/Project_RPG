using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface ILegacyInventoryRepository
{
    Task<IReadOnlyList<InventoryItem>> LoadInventoryAsync(
        Guid characterId,
        CancellationToken cancellationToken);

    Task SaveInventoryAsync(
        Guid characterId,
        IReadOnlyList<InventoryItem> inventory,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken,
        // HTTP writes must supply their authenticated assignment. Null is for
        // trusted in-process legacy import/fixtures, never a request-body option.
        GameServerCredential? requiredServer = null);
}
