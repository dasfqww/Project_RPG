using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Configuration;

public sealed class RidingOptions
{
    public bool Enabled { get; set; }
    public int CatalogVersion { get; set; }
    public string? GameplayHash { get; set; }
    public RidingGameplayPolicy Policy { get; set; } = new();
    public RidingMountOptions[] Mounts { get; set; } = [];
    public RidingRegistrationOptions[] RegistrationItems { get; set; } = [];

    public RidingCatalog? BuildCatalog()
    {
        if (!Enabled) return null;
        if (GameplayHash is null) throw new InvalidOperationException("Enabled Riding requires a verified gameplay hash.");
        return new(1, CatalogVersion, Policy, Mounts.Select(m => new RidingMountDefinition(m.MountId, m.DefinitionVersion, m.Enabled, m.RegionTags)).ToArray(),
            RegistrationItems.Select(m => new RidingRegistrationDefinition(new(m.DefinitionType, m.DefinitionName, m.DefinitionVersion), m.MountId, m.MaximumStackSize, m.Uses)).ToArray(), GameplayHash);
    }
}

// A frozen startup snapshot; mutable configuration objects are never consulted during a request.
public sealed record RidingRuntime(RidingCatalog? Catalog);

public sealed class RidingMountOptions
{
    public string MountId { get; set; } = "";
    public int DefinitionVersion { get; set; }
    public bool Enabled { get; set; } = true;
    public string[] RegionTags { get; set; } = [];
}
public sealed class RidingRegistrationOptions
{
    public string DefinitionType { get; set; } = "";
    public string DefinitionName { get; set; } = "";
    public int DefinitionVersion { get; set; }
    public string MountId { get; set; } = "";
    public int MaximumStackSize { get; set; }
    public RidingItemUse Uses { get; set; } = RidingItemUse.RegisterMount;
}
