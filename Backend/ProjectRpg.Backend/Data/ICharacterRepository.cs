using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface ICharacterRepository : ICharacterOwnershipRepository, ICharacterEconomyRepository
{
    Task<IReadOnlyList<GameCharacter>> GetCharactersAsync(
        string steamId,
        CancellationToken cancellationToken);

    Task<IReadOnlyList<Roster>> GetRostersAsync(
        string steamId,
        CancellationToken cancellationToken);

    Task<GameCharacter> CreateCharacterAsync(
        string steamId,
        string name,
        DateTimeOffset createdAt,
        CancellationToken cancellationToken);
}
