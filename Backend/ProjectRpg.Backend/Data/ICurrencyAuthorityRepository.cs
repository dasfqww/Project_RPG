namespace ProjectRpg.Backend.Data;

/// <summary>Character economy context and dungeon membership needed to authorize a currency command.</summary>
public interface ICurrencyAuthorityRepository : ICharacterEconomyRepository, IGameServerMembershipRepository
{
}
