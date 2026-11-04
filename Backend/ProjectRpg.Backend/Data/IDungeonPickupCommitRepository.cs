using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

/// <summary>Revalidates membership and consumes the frozen intent with its item receipt atomically.</summary>
public interface IDungeonPickupCommitRepository
{
    Task<DungeonPickupCommitResult> CommitAsync(Guid sessionId, string serverId, Guid requestId,
        CancellationToken cancellationToken);
}
