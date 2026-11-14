using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Api;

public static partial class RidingApiEndpoints
{
    private sealed record RegistrationOption(string DefinitionType, string DefinitionName, int DefinitionVersion,
        string MountId, int MountDefinitionVersion, int RequiredQuantity, int MaximumStackSize, bool AlreadyOwned);

    private static void MapRidingRegistrationOptionsApi(WebApplication app)
    {
        app.MapGet(Root + "/registration-options",
            (Guid characterId, HttpContext c, IRidingPreferencesStore store, RidingRuntime runtime, RidingRequestLimiter limiter, CancellationToken ct) =>
            Run(c, limiter, false, async account =>
            {
                // Apply the same current account/character/roster authorization as the collection read, even when disabled.
                var view = await store.GetRidingOwnerViewAsync(account, characterId, ct);
                var owned = view.Collection.Mounts.Select(m => m.MountId).ToHashSet(StringComparer.Ordinal);
                var catalog = runtime.Catalog; var options = new List<RegistrationOption>();
                if (catalog is not null)
                    foreach (var registration in catalog.Registrations)
                    {
                        var mount = catalog.Mount(registration.MountId);
                        if (!mount.Enabled) continue;
                        options.Add(new(registration.Item.Type, registration.Item.Name, registration.Item.Version,
                            mount.MountId, mount.DefinitionVersion, 1, registration.MaximumStackSize, owned.Contains(mount.MountId)));
                    }
                // Definition metadata and ownership observation only. Current inventory/lease/CAS checks remain in POST.
                return new
                {
                    protocolVersion = 1, characterId, view.Collection.RosterId,
                    rosterRevision = RidingApiContracts.Number(view.Collection.Revision),
                    catalogReady = catalog is not null, catalogVersion = catalog?.CatalogVersion, gameplayHash = catalog?.GameplayHash,
                    registrationEnabled = catalog is not null, options
                };
            }));
    }
}
