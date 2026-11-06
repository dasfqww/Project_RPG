namespace ProjectRpg.Backend.Data;

// A frozen settlement recipient is the existing authority for a successful dungeon result.
// Reward delivery may still be pending; its retries never create another clear event.
public sealed record NarrativeDungeonClearEvidence(Guid DungeonSessionId, string ServerId,
    Guid CharacterId, string SteamId, string DungeonId, string Difficulty, DateTimeOffset OccurredAt);

public interface INarrativeDungeonEvidenceRepository
{
    Task<NarrativeDungeonClearEvidence?> LoadNarrativeDungeonClearEvidenceAsync(
        Guid dungeonSessionId, Guid characterId, CancellationToken cancellationToken);
}
