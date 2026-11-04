using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

/// <summary>Storage composition facade. Application services depend on the focused contracts.</summary>
public interface IGameRepository : IAuthenticationRepository, ICharacterRepository, IDungeonSessionRepository,
    IDungeonSettlementRepository, IJoinTicketRepository, ILegacyInventoryRepository, ICurrencyAuthorityRepository
{
    Task EnsureCreatedAsync(CancellationToken cancellationToken);
}
