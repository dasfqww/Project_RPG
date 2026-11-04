using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface ICharacterOwnershipRepository
{
    Task<string?> GetCharacterOwnerAsync(
        Guid characterId,
        CancellationToken cancellationToken);
}
