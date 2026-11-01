using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface ICharacterProgressionRepository
{
    // Own characters only. Never awards XP, migrates saves or initializes missing records.
    Task<IReadOnlyDictionary<Guid, CharacterProgressionSummary>> GetStoredProgressionSummariesAsync(
        string steamId, CancellationToken cancellationToken);
    // Reconcile only eligible Completed settlements; missing legacy records stay missing.
    Task<CharacterProgressionState?> LoadProgressionAsync(Guid characterId, DateTimeOffset now,
        CancellationToken cancellationToken);
    Task<CharacterProgressionState> CommitSkillConfigurationAsync(Guid characterId, string steamId,
        SkillConfigurationCommand command, DateTimeOffset now, CancellationToken cancellationToken);
}
