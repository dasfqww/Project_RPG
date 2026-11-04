using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface ICharacterEconomyRepository
{
    Task<CharacterEconomyContext?> GetCharacterEconomyContextAsync(
        Guid characterId,
        CancellationToken cancellationToken);
}
