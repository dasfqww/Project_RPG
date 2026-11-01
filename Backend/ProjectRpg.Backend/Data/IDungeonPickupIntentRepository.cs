using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IDungeonPickupIntentRepository
{
    // Current membership validation and reservation storage share the same lock
    // as abandonment/settlement. Neither method writes item records or receipts.
    Task<DungeonPickupIntentResult> PreparePickupAsync(Guid sessionId, string serverId, Guid dropId,
        ItemRepositoryCommitRequest command, DateTimeOffset now, CancellationToken cancellationToken);
    Task<DungeonPickupIntentResult> ReadPickupIntentAsync(Guid sessionId, string serverId, Guid requestId,
        DateTimeOffset now, CancellationToken cancellationToken);
}
