using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public static class AuctionRepositoryRegistration
{
    public static IServiceCollection AddAuctionServices(this IServiceCollection services, AuctionOptions options, bool postgres)
    {
        options.Validate();
        services.AddSingleton<IOptions<AuctionOptions>>(Options.Create(options));
        services.AddSingleton(new AuctionEscrowLimits(options.MaximumOpenListingsPerAccount, options.MaximumOwnedRecords));
        if (postgres)
        {
            services.AddSingleton<PostgresAuctionEscrowRepository>();
            services.AddSingleton<IAuctionEscrowRepository>(p => p.GetRequiredService<PostgresAuctionEscrowRepository>());
            services.AddSingleton<IAuctionPurchaseRepository>(p => p.GetRequiredService<PostgresAuctionEscrowRepository>());
            services.AddSingleton<IAuctionClaimRepository, PostgresAuctionClaimRepository>();
            services.AddSingleton<IAuctionQueryRepository, PostgresAuctionQueryRepository>();
        }
        else
        {
            services.AddSingleton<InMemoryAuctionRepository>(p => new(p.GetRequiredService<InMemoryTransactionGate>(), (InMemoryGameRepository)p.GetRequiredService<IGameRepository>(),
                p.GetRequiredService<InMemoryItemRepository>(), p.GetRequiredService<InMemoryEconomyRepository>(), p.GetRequiredService<TradeItemPolicy>(), p.GetRequiredService<AuctionEscrowLimits>()));
            services.AddSingleton<IAuctionEscrowRepository>(p => p.GetRequiredService<InMemoryAuctionRepository>());
            services.AddSingleton<IAuctionPurchaseRepository>(p => p.GetRequiredService<InMemoryAuctionRepository>());
            services.AddSingleton<IAuctionClaimRepository>(p => p.GetRequiredService<InMemoryAuctionRepository>());
            services.AddSingleton<IAuctionQueryRepository>(p => p.GetRequiredService<InMemoryAuctionRepository>());
        }
        services.AddSingleton<AuctionApplication>(); services.AddSingleton<AuctionRequestLimiter>();
        services.AddSingleton<AuctionExpiryWorker>();
        services.AddHostedService(p => p.GetRequiredService<AuctionExpiryWorker>());
        return services;
    }
}
