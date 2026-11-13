namespace ProjectRpg.Backend.Riding;

// Point-in-time evidence for ONE confirmed connection, never a portable player-supplied ownership grant.
public sealed record RidingAdmissionSnapshot(Guid DungeonSessionId, Guid CharacterId, Guid RosterId,
    long ConnectionGeneration, DateTimeOffset ObservedAtUtc, DateTimeOffset ValidUntilUtc, RidingOwnerView Owner);
public interface IRidingAdmissionSnapshotStore
{
    Task<RidingAdmissionSnapshot?> GetRidingAdmissionSnapshotAsync(string serverId, Guid dungeonSessionId,
        Guid characterId, long connectionGeneration, CancellationToken cancellationToken);
}
