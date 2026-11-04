using Microsoft.Extensions.DependencyInjection.Extensions;

namespace ProjectRpg.Backend.Data;

public static class GameRepositoryContracts
{
    /// <summary>Expose focused views of the same store; never create a second state or transaction owner.</summary>
    public static IServiceCollection AddGameRepositoryContracts(this IServiceCollection services)
    {
        services.TryAddSingleton<IAuthenticationRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<ICharacterOwnershipRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<ICharacterEconomyRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<ICharacterRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<IDungeonMembershipRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<IGameServerMembershipRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<IDungeonSessionRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<IDungeonSettlementRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<IJoinTicketRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<ILegacyInventoryRepository>(p => p.GetRequiredService<IGameRepository>());
        services.TryAddSingleton<ICurrencyAuthorityRepository>(p => p.GetRequiredService<IGameRepository>());
        return services;
    }
}
