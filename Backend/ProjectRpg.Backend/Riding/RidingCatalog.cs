using System.Globalization;
using System.Text;

namespace ProjectRpg.Backend.Riding;

public sealed record RidingGameplayPolicy(float RideSpeed = 600, float Acceleration = 2048, float Braking = 2000,
    float YawRate = 500, float DashDistance = 900, float DashDuration = .6f, float DashCooldown = 10,
    float CombatLock = 10, float EmoteDuration = 3, float EmoteCooldown = 2);

public sealed record RidingMountDefinition(string MountId, int DefinitionVersion, bool Enabled, IReadOnlyList<string> RegionTags);
public sealed record RidingItemDefinitionKey(string Type, string Name, int Version);
[Flags]
public enum RidingItemUse { None = 0, RegisterMount = 1, ConsumeEffects = 2, Equipment = 4 }
public sealed record RidingRegistrationDefinition(RidingItemDefinitionKey Item, string MountId, int MaximumStackSize, RidingItemUse Uses);

/// <summary>Immutable server catalog. Construction validates data but does not enable an API or grant ownership.</summary>
public sealed class RidingCatalog
{
    private readonly Dictionary<string, RidingMountDefinition> mounts;
    private readonly Dictionary<RidingItemDefinitionKey, RidingRegistrationDefinition> registrations;
    private readonly string canonical;
    public int ProtocolVersion { get; }
    public int CatalogVersion { get; }
    public RidingGameplayPolicy Policy { get; }
    public IReadOnlyList<RidingMountDefinition> Mounts { get; }
    public IReadOnlyList<RidingRegistrationDefinition> Registrations { get; }
    public string GameplayHash { get; }

    public RidingCatalog(int protocolVersion, int catalogVersion, RidingGameplayPolicy policy,
        IReadOnlyList<RidingMountDefinition> mountDefinitions, IReadOnlyList<RidingRegistrationDefinition> registrationDefinitions,
        string? expectedGameplayHash = null)
    {
        if (protocolVersion != 1 || catalogVersion <= 0 || policy is null || mountDefinitions is null
            || registrationDefinitions is null || mountDefinitions.Count is < 1 or > 512 || registrationDefinitions.Count > 512)
            throw new RidingRuleException(RidingFailure.InvalidCatalog);
        Validate(policy);
        ProtocolVersion = protocolVersion; CatalogVersion = catalogVersion; Policy = policy with { };
        mounts = new(StringComparer.Ordinal); registrations = [];
        foreach (var mount in mountDefinitions)
        {
            if (mount is null || !RidingIdentity.Identifier(mount.MountId, 64, true) || mount.DefinitionVersion <= 0
                || mount.RegionTags is null || mount.RegionTags.Count > 64 || mount.RegionTags.Any(t => !RidingIdentity.RegionTag(t))
                || mount.RegionTags.Distinct(StringComparer.Ordinal).Count() != mount.RegionTags.Count)
                throw new RidingRuleException(RidingFailure.InvalidCatalog);
            var snapshot = mount with { RegionTags = Array.AsReadOnly(mount.RegionTags.Order(StringComparer.Ordinal).ToArray()) };
            if (!mounts.TryAdd(snapshot.MountId, snapshot)) throw new RidingRuleException(RidingFailure.InvalidCatalog);
        }
        foreach (var definition in registrationDefinitions)
        {
            if (definition is null || definition.Item is null || !RidingIdentity.Identifier(definition.Item.Type, 64)
                || !RidingIdentity.Identifier(definition.Item.Name, 128) || definition.Item.Version <= 0
                || definition.MaximumStackSize <= 0 || definition.Uses != RidingItemUse.RegisterMount
                || definition.MountId is null || !mounts.ContainsKey(definition.MountId)
                || !registrations.TryAdd(definition.Item, definition with { Item = definition.Item with { } }))
                throw new RidingRuleException(RidingFailure.InvalidCatalog);
        }
        Mounts = Array.AsReadOnly(mounts.Values.OrderBy(m => m.MountId, StringComparer.Ordinal).ToArray());
        Registrations = Array.AsReadOnly(registrations.Values.OrderBy(m => m.Item.Type, StringComparer.Ordinal)
            .ThenBy(m => m.Item.Name, StringComparer.Ordinal).ThenBy(m => m.Item.Version).ToArray());
        canonical = Canonicalize(); GameplayHash = RidingIdentity.Sha256(canonical);
        if (expectedGameplayHash is not null && (!RidingIdentity.Hash(expectedGameplayHash) || expectedGameplayHash != GameplayHash))
            throw new RidingRuleException(RidingFailure.DefinitionMismatch);
    }

    public void RequireCompatible(int version, string hash)
    {
        if (version != CatalogVersion || !RidingIdentity.Hash(hash) || hash != GameplayHash)
            throw new RidingRuleException(RidingFailure.DefinitionMismatch);
    }

    public RidingMountDefinition Mount(string mountId) => RidingIdentity.Identifier(mountId, 64, true) && mounts.TryGetValue(mountId, out var value) ? value
        : throw new RidingRuleException(RidingFailure.DefinitionMismatch);
    public RidingRegistrationDefinition Registration(RidingItemDefinitionKey item) => item is not null && registrations.TryGetValue(item, out var value) ? value
        : throw new RidingRuleException(RidingFailure.DefinitionMismatch);

    // Fresh bytes; callers cannot change the hash projection retained by this catalog.
    public byte[] CanonicalGameplayBytes() => Encoding.UTF8.GetBytes(canonical);

    private static void Validate(RidingGameplayPolicy p)
    {
        static bool Range(float v, float min, float max) => float.IsFinite(v) && v >= min && v <= max;
        if (!Range(p.RideSpeed, 1, 5000) || !Range(p.Acceleration, 1, 20000) || !Range(p.Braking, 1, 20000)
            || !Range(p.YawRate, 1, 1080) || !Range(p.DashDistance, 1, 5000) || !Range(p.DashDuration, .05f, 5)
            || !Range(p.DashCooldown, .05f, 10) || p.DashDuration > p.DashCooldown || !Range(p.CombatLock, .05f, 60)
            || !Range(p.EmoteDuration, .05f, 3) || !Range(p.EmoteCooldown, .05f, 2))
            throw new RidingRuleException(RidingFailure.InvalidCatalog);
    }

    private string Canonicalize()
    {
        var b = new StringBuilder("ProjectRPG.Riding.Gameplay/v1\n");
        void Integer(string name, int value) => b.Append(name).Append('=').Append(value.ToString(CultureInfo.InvariantCulture)).Append('\n');
        void Number(string name, float value) => b.Append(name).Append('=').Append(RidingIdentity.FloatBits(value)).Append('\n');
        Integer("protocol", ProtocolVersion); Integer("catalog", CatalogVersion);
        Number("rideSpeed", Policy.RideSpeed); Number("acceleration", Policy.Acceleration); Number("braking", Policy.Braking);
        Number("yawRate", Policy.YawRate); Number("dashDistance", Policy.DashDistance); Number("dashDuration", Policy.DashDuration);
        Number("dashCooldown", Policy.DashCooldown); Number("combatLock", Policy.CombatLock);
        Number("emoteDuration", Policy.EmoteDuration); Number("emoteCooldown", Policy.EmoteCooldown);
        foreach (var m in Mounts)
            b.Append("mount=").Append(m.MountId).Append('|').Append(m.DefinitionVersion.ToString(CultureInfo.InvariantCulture))
                .Append('|').Append(m.Enabled ? '1' : '0').Append('|').AppendJoin(',', m.RegionTags).Append('\n');
        foreach (var r in Registrations)
            b.Append("register=").Append(r.Item.Type).Append('|').Append(r.Item.Name).Append('|')
                .Append(r.Item.Version.ToString(CultureInfo.InvariantCulture)).Append('|').Append(r.MountId).Append('|')
                .Append(r.MaximumStackSize.ToString(CultureInfo.InvariantCulture)).Append('|').Append(((int)r.Uses).ToString(CultureInfo.InvariantCulture)).Append('\n');
        return b.ToString();
    }
}
