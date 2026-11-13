using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Data;

public static class RidingRegistrationServices
{
    public static IServiceCollection AddRidingServices(this IServiceCollection services, RidingOptions options)
    {
        services.AddSingleton(new RidingRuntime(options.BuildCatalog()));
        // Reuse the ONE game/item composition. Never create a second Memory game or independent asset gate.
        services.AddSingleton<IRidingRegistrationStore>(p => (IRidingRegistrationStore)p.GetRequiredService<IGameRepository>());
        services.AddSingleton<IRidingPreferencesStore>(p => (IRidingPreferencesStore)p.GetRequiredService<IGameRepository>());
        services.AddSingleton<IRidingAdmissionSnapshotStore>(p => (IRidingAdmissionSnapshotStore)p.GetRequiredService<IGameRepository>());
        services.AddSingleton<RidingRequestLimiter>();
        return services;
    }
}
