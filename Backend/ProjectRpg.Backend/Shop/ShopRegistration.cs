using Npgsql;
using ProjectRpg.Backend.Data;

namespace ProjectRpg.Backend.Shop;

public static class ShopRegistration
{
    public static void AddShopServices(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddSingleton(ShopCatalogConfiguration.Load(configuration));
        services.AddSingleton<ShopRequestLimiter>();
        services.AddSingleton<IShopStore>(provider => provider.GetService<NpgsqlDataSource>() is { } source
            ? new PostgresShopStore(source)
            : new InMemoryShopStore(provider.GetRequiredService<InMemoryTransactionGate>(),
                (InMemoryGameRepository)provider.GetRequiredService<IGameRepository>(),
                provider.GetRequiredService<InMemoryItemRepository>(), provider.GetRequiredService<InMemoryEconomyRepository>()));
    }
}
