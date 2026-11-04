using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Inventory;

public enum InventoryContainer { Inventory, Equipment, Mail }
public sealed record DurabilitySummary(int Current, int Maximum);
public sealed record ItemSummary(Guid ItemId, string DefinitionType, string DefinitionName, int DefinitionVersion,
    int Quantity, long Revision, string ContainerType, string ContainerId, int SlotIndex,
    string LifecycleState, string BindState, bool IsLocked, DateTimeOffset? ExpiresAtUtc, DurabilitySummary Durability);
public interface IInventoryReadStore
{
    Task<Result<Page<ItemSummary>>> ReadAsync(Guid characterId, InventoryContainer container,
        PageRequest page, CancellationToken cancellationToken);
}
public sealed class GetInventoryPage(IOperatorAccess access, IInventoryReadStore store)
{
    public async Task<Result<Page<ItemSummary>>> ExecuteAsync(Guid id, InventoryContainer container, PageRequest page, CancellationToken ct)
    {
        Result<OperatorActor> actor = await access.AuthorizeAsync(OpsCapability.ReadCharacter, ct);
        if (!actor.IsSuccess) return Result<Page<ItemSummary>>.Fail(actor.Error!);
        if (id == Guid.Empty || !Enum.IsDefined(container) || !page.IsValid() || page.After?.Timestamp is not null)
            return Result<Page<ItemSummary>>.Fail(Failures.InvalidQuery);
        return await store.ReadAsync(id, container, page, ct);
    }
}
