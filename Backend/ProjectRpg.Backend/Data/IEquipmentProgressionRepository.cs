using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IEquipmentProgressionRepository
{
    Task<ItemRepositoryCommitResult> CraftEquipmentAsync(string steamId, FirstSliceEquipmentCraftCommand command,
        DateTimeOffset now, CancellationToken cancellationToken);
    Task<ItemRepositoryCommitResult> DismantleEquipmentAsync(string steamId, FirstSliceEquipmentDismantleCommand command,
        DateTimeOffset now, CancellationToken cancellationToken);
    Task<ItemRepositoryCommitResult> UpgradeEquipmentAsync(string steamId, FirstSliceEquipmentUpgradeCommand command,
        DateTimeOffset now, CancellationToken cancellationToken);
    Task<ItemRepositoryCommitResult> TransferEquipmentAsync(string steamId, FirstSliceEquipmentTransferCommand command,
        DateTimeOffset now, CancellationToken cancellationToken);
}
