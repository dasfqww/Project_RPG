using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Riding;

static class Program
{
    static int assertions;
    static readonly List<object> cases = [];
    static readonly RidingRegistrationActor Actor = new(Guid.Parse("10000000-0000-0000-0000-000000000001"), Guid.Parse("20000000-0000-0000-0000-000000000001"));
    static readonly DateTimeOffset Now = DateTimeOffset.Parse("2026-11-13T00:00:00Z", CultureInfo.InvariantCulture);
    static RidingMountDefinition Horse => new("horse", 3, true, ["Region.Field", "Region.Town"]);
    static RidingMountDefinition Wolf => new("wolf", 2, true, []);
    static RidingRegistrationDefinition Mapping => new(new("RPGItemDefinition", "GameItem.Consume.Mount.Horse", 4), "horse", 99, RidingItemUse.RegisterMount);
    static RidingCatalog Catalog(RidingGameplayPolicy? policy = null) => new(1, 7, policy ?? new(), [Horse, Wolf], [Mapping]);
    static ItemRecord Item(int quantity = 2) => new("RPGItemDefinition", "GameItem.Consume.Mount.Horse", 4,
        new(ItemOwnerType.Character, Actor.CharacterId.ToString("D")), new(ItemContainerType.Inventory, Actor.CharacterId.ToString("D"), 80),
        new(Guid.Parse("30000000-0000-0000-0000-000000000001"), 19, quantity, ["Item.Rarity.Rare"], [new("Item.Stat.Test", 5)]),
        17, ItemLifecycleState.Active, new(ItemBindState.CharacterBound, new(0, 0), Now.AddDays(1), "WorldPickup", false));
    static RidingRegistrationCommand Command => new(Guid.Parse("40000000-0000-0000-0000-000000000001"), Item().ItemId, 17, 7);
    static RidingRegistrationPlan Plan(ItemRecord? item = null, RidingCatalog? catalog = null, long revision = 4,
        IReadOnlySet<string>? owned = null, bool reserved = false, RidingRegistrationCommand? command = null) =>
        RidingRegistrationRules.Plan(Actor, command ?? Command, catalog ?? Catalog(), item ?? Item(), revision, owned ?? new HashSet<string>(), reserved, Now);
    static void Check(bool condition, string reason)
    { assertions++; if (!condition) throw new InvalidOperationException(reason); }
    static void Reject(RidingFailure code, Action action)
    {
        assertions++;
        try { action(); } catch (RidingRuleException e) when (e.Code == code) { return; }
        throw new InvalidOperationException("Expected riding failure " + code);
    }
    static void Case(string name, Action test)
    { int before = assertions; test(); cases.Add(new { Name = name, Assertions = assertions - before, Passed = true }); }
    static int Main()
    {
        try
        {
            CatalogCases(); RegistrationCases();
            Console.WriteLine(JsonSerializer.Serialize(new { Passed = true, Scope = "Catalog and transaction proposal only; no store/API/auth/PG/UE authority", CaseCount = cases.Count, Assertions = assertions, Cases = cases }));
            return 0;
        }
        catch (Exception e)
        { Console.Error.WriteLine(e); Console.WriteLine(JsonSerializer.Serialize(new { Passed = false, Assertions = assertions, Cases = cases, Error = e.Message })); return 1; }
    }

    static void CatalogCases()
    {
        Case("canonical_order_and_culture", () =>
        {
            var original = Catalog(); var culture = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("ar-SA");
                var reordered = new RidingCatalog(1, 7, new(), [Wolf, Horse with { RegionTags = ["Region.Town", "Region.Field"] }], [Mapping]);
                Check(original.GameplayHash == reordered.GameplayHash, "Definition/tag order and culture must not alter manifest");
                Check(original.GameplayHash == "2f0ac94d23e4e896dfcb42250843746be1d56fe727623b1d0048448842c19f0d", "Independent canonical byte/hash reference vector");
                Check(original.GameplayHash == Convert.ToHexString(SHA256.HashData(original.CanonicalGameplayBytes())).ToLowerInvariant(), "Hash uses the exported exact bytes");
                var text = Encoding.UTF8.GetString(original.CanonicalGameplayBytes());
                Check(text.StartsWith("ProjectRPG.Riding.Gameplay/v1\nprotocol=1\ncatalog=7\nrideSpeed=44160000\n", StringComparison.Ordinal), "Versioned LF/IEEE-754 projection");
                Check(!text.Contains('\r') && text.EndsWith('\n'), "Canonical LF and terminal newline");
            }
            finally { CultureInfo.CurrentCulture = culture; }
        });
        Case("catalog_defensive_snapshot", () =>
        {
            var tags = new List<string> { "Region.Field" }; var mounts = new List<RidingMountDefinition> { Horse with { RegionTags = tags } };
            var mappings = new List<RidingRegistrationDefinition> { Mapping }; var catalog = new RidingCatalog(1, 7, new(), mounts, mappings);
            var hash = catalog.GameplayHash; tags.Clear(); mounts.Clear(); mappings.Clear();
            Check(catalog.Mount("horse").RegionTags.SequenceEqual(new[] { "Region.Field" }) && catalog.Registrations.Count == 1, "Input mutation cannot affect catalog");
            var bytes = catalog.CanonicalGameplayBytes(); bytes[0] = 0;
            Check(catalog.GameplayHash == hash && catalog.CanonicalGameplayBytes()[0] != 0, "Export is a fresh byte snapshot");
            Check(((IList<RidingMountDefinition>)catalog.Mounts).IsReadOnly && ((IList<string>)catalog.Mounts[0].RegionTags).IsReadOnly, "Output collections cannot be edited");
        });
        Case("catalog_compatibility", () =>
        {
            var c = Catalog(); c.RequireCompatible(7, c.GameplayHash);
            var loaded = new RidingCatalog(1, 7, new(), [Horse, Wolf], [Mapping], c.GameplayHash);
            Check(loaded.GameplayHash == c.GameplayHash, "Declared manifest hash matches");
            Reject(RidingFailure.DefinitionMismatch, () => c.RequireCompatible(8, c.GameplayHash));
            Reject(RidingFailure.DefinitionMismatch, () => c.RequireCompatible(7, c.GameplayHash.ToUpperInvariant()));
            Reject(RidingFailure.DefinitionMismatch, () => new RidingCatalog(1, 7, new(), [Horse, Wolf], [Mapping], new string('a', 64)));
            float next = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(600f) + 1);
            Check(c.GameplayHash != Catalog(new() { RideSpeed = next }).GameplayHash, "One gameplay float bit changes hash");
            Check(c.GameplayHash != new RidingCatalog(1, 7, new(), [Horse with { DefinitionVersion = 4 }, Wolf], [Mapping]).GameplayHash, "Definition version changes hash");
            Check(c.GameplayHash != new RidingCatalog(1, 8, new(), [Horse, Wolf], [Mapping]).GameplayHash, "Catalog deployment changes hash");
        });
        Case("catalog_shape_and_limits", () =>
        {
            foreach (var id in new[] { "", "Horse", ".horse", "horse|wolf", "horse\n", new string('a', 65), "말" })
                Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse with { MountId = id }], []));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(2, 1, new(), [Horse], []));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 0, new(), [Horse], []));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [], []));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse, Horse], []));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse with { RegionTags = ["A", "A"] }], []));
            foreach (var tag in new[] { ".A", "A.", "A..B", "A|B", "A B" })
                Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse with { RegionTags = [tag] }], []));
            var limit = Enumerable.Range(0, 512).Select(i => new RidingMountDefinition("m" + i.ToString(CultureInfo.InvariantCulture), 1, true, [])).ToArray();
            Check(new RidingCatalog(1, 1, new(), limit, []).Mounts.Count == 512, "512 mount catalog accepted");
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [.. limit, new("overflow", 1, true, [])], []));
            Reject(RidingFailure.DefinitionMismatch, () => Catalog().Mount(null!));
        });
        Case("catalog_mapping_exclusivity", () =>
        {
            foreach (var usage in new[] { RidingItemUse.None, RidingItemUse.Equipment, RidingItemUse.RegisterMount | RidingItemUse.ConsumeEffects,
                RidingItemUse.RegisterMount | RidingItemUse.Equipment, (RidingItemUse)8 })
                Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse], [Mapping with { Uses = usage }]));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse], [Mapping, Mapping]));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse], [Mapping with { MountId = "missing" }]));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse], [Mapping with { MaximumStackSize = 0 }]));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse], [Mapping with { Item = new("Bad|Type", "Item.X", 1) }]));
            Reject(RidingFailure.InvalidCatalog, () => new RidingCatalog(1, 1, new(), [Horse], [Mapping with { Item = null! }]));
        });
        Case("policy_bounds_match_ue", () =>
        {
            foreach (var p in new[] { new RidingGameplayPolicy { RideSpeed = float.NaN }, new() { Acceleration = float.PositiveInfinity },
                new() { Braking = 0 }, new() { YawRate = 1081 }, new() { DashDistance = 5001 }, new() { DashDuration = .01f },
                new() { DashDuration = 2, DashCooldown = 1 }, new() { DashCooldown = 11 }, new() { CombatLock = 61 },
                new() { EmoteDuration = 3.1f }, new() { EmoteCooldown = 2.1f } })
                Reject(RidingFailure.InvalidCatalog, () => Catalog(p));
            Check(Catalog(new() { RideSpeed = 5000, Acceleration = 20000, Braking = 20000, YawRate = 1080, DashDistance = 5000,
                DashDuration = 5, DashCooldown = 10, CombatLock = 60, EmoteDuration = 3, EmoteCooldown = 2 }).Policy.DashDuration == 5, "UE policy upper bounds");
        });
    }

    static void RegistrationCases()
    {
        Case("registration_stack_plan", () =>
        {
            var source = Item(); var plan = Plan(source); var mutation = plan.Consumption!;
            Check(plan.Outcome == RidingRegistrationOutcome.Registered && plan.ExpectedRosterRevision == 4 && plan.NewRosterRevision == 5, "New unlock advances roster once");
            Check(mutation.ExpectedRevision == 17 && mutation.NewRecord.Revision == 17 && mutation.NewRecord.State.Quantity == 1, "One item with common commit CAS convention");
            Check(source.State.Quantity == 2 && source.Revision == 17, "Planner never mutates inventory");
            Check(mutation.NewRecord.Location == source.Location && mutation.NewRecord.Metadata == source.Metadata, "Preserve stack location/bind/provenance");
            Check(plan.Unlock == new RidingUnlock("horse", 3, Now, Command.RequestId) && plan.ChildItemRequestId.HasValue, "Parent unlock provenance");
            var commit = new ItemRepositoryCommitRequest(plan.ChildItemRequestId!.Value, RidingRegistrationRules.Operation, plan.Fingerprint,
                source.Owner, 1, [mutation]);
            Check(ItemRecordRules.TryValidateCommit(commit, out _), "Real Item V2 commit schema accepts proposal");
        });
        Case("registration_tombstone_and_snapshot", () =>
        {
            var tags = new List<string> { "Item.Rarity.Rare" }; var stats = new List<ItemStatValue> { new("Item.Stat.Test", 5) };
            var source = Item(1); source = source with { State = source.State with { InstanceTags = tags, StatValues = stats } };
            var consumed = Plan(source).Consumption!.NewRecord; tags.Clear(); stats.Clear();
            Check(consumed.State.Quantity == 0 && consumed.LifecycleState == ItemLifecycleState.Consumed && consumed.Location == new ItemLocation(ItemContainerType.Terminal, "", -1), "Last item becomes standard tombstone");
            Check(consumed.State.InstanceTags.Count == 1 && consumed.State.StatValues.Count == 1, "Proposal owns metadata arrays");
            Check(((IList<string>)consumed.State.InstanceTags).IsReadOnly && ((IList<ItemStatValue>)consumed.State.StatValues).IsReadOnly, "Plan lists are read-only");
            Check(ItemRecordRules.TryValidateRecord(consumed, out _), "Tombstone conforms to existing record rules");
        });
        Case("already_owned_is_noop", () =>
        {
            var plan = Plan(owned: new HashSet<string> { "horse" });
            Check(plan.Outcome == RidingRegistrationOutcome.AlreadyOwned && plan.NewRosterRevision == 4 && plan.Consumption is null
                && plan.Unlock is null && plan.ChildItemRequestId is null, "AlreadyOwned consumes nothing and emits no child");
            Check(Plan(revision: long.MaxValue, owned: new HashSet<string> { "horse" }).NewRosterRevision == long.MaxValue, "No-op does not overflow revision");
            Reject(RidingFailure.AssetReserved, () => Plan(owned: new HashSet<string> { "horse" }, reserved: true));
            Reject(RidingFailure.RevisionConflict, () => Plan(owned: new HashSet<string> { "horse" }, command: Command with { ExpectedItemRevision = 16 }));
        });
        Case("registration_inventory_and_availability", () =>
        {
            var item = Item();
            Reject(RidingFailure.WrongInventory, () => Plan(item with { Owner = new(ItemOwnerType.Character, Guid.NewGuid().ToString("D")) }));
            foreach (var container in new[] { ItemContainerType.Mail, ItemContainerType.CharacterStorage, ItemContainerType.Trade, ItemContainerType.Auction, ItemContainerType.Equipment })
                Reject(RidingFailure.WrongInventory, () => Plan(item with { Location = item.Location with { ContainerType = container } }));
            Reject(RidingFailure.AssetReserved, () => Plan(reserved: true));
            Reject(RidingFailure.WrongInventory, () => Plan(item with { Location = item.Location with { SlotIndex = 0 } }));
            Reject(RidingFailure.ItemLocked, () => Plan(item with { Metadata = item.Metadata with { IsLocked = true } }));
            Reject(RidingFailure.ItemExpired, () => Plan(item with { Metadata = item.Metadata with { ExpiresAtUtc = Now } }));
            Reject(RidingFailure.ItemUnavailable, () => Plan(item with { State = item.State with { Quantity = 0 } }));
            Reject(RidingFailure.DefinitionMismatch, () => Plan(Item(100)));
            Reject(RidingFailure.RevisionConflict, () => Plan(command: Command with { ExpectedItemRevision = 16 }));
            Reject(RidingFailure.DefinitionMismatch, () => Plan(command: Command with { CatalogVersion = 8 }));
            Reject(RidingFailure.DefinitionMismatch, () => Plan(item with { DefinitionVersion = 5 }));
            foreach (ItemBindState bind in Enum.GetValues<ItemBindState>())
                Check(Plan(item with { Metadata = item.Metadata with { BindState = bind } }).Outcome == RidingRegistrationOutcome.Registered, "Binding is not a trade eligibility rule for owned registration");
        });
        Case("registration_fail_closed_shapes_and_limits", () =>
        {
            var item = Item();
            foreach (var malformed in new[] { item with { State = null! }, item with { Metadata = null! }, item with { Location = null! },
                item with { State = item.State with { StatValues = [null!] } }, item with { Metadata = item.Metadata with { Durability = null! } } })
                Reject(RidingFailure.ItemUnavailable, () => Plan(malformed));
            Reject(RidingFailure.InvalidRequest, () => RidingRegistrationRules.Fingerprint(Actor with { RosterId = Guid.Empty }, Command));
            Reject(RidingFailure.InvalidRequest, () => Plan(command: Command with { ExpectedItemRevision = long.MaxValue }));
            Reject(RidingFailure.InvalidRequest, () => Plan(command: Command with { RequestId = Guid.Empty }));
            Reject(RidingFailure.RosterRevisionOverflow, () => Plan(revision: long.MaxValue));
            Reject(RidingFailure.InvalidRequest, () => Plan(revision: -1));
            var full = Enumerable.Range(0, 512).Select(i => "m" + i.ToString(CultureInfo.InvariantCulture)).ToHashSet();
            Reject(RidingFailure.RosterLimitReached, () => Plan(owned: full));
            Reject(RidingFailure.DefinitionMismatch, () => Plan(catalog: new(1, 7, new(), [Horse with { Enabled = false }], [Mapping])));
        });
        Case("command_fingerprint_and_child_identity", () =>
        {
            var fingerprint = RidingRegistrationRules.Fingerprint(Actor, Command);
            Check(fingerprint == Plan().Fingerprint && fingerprint.Length == 64, "Deterministic parent fingerprint");
            foreach (var cmd in new[] { Command with { ItemId = Guid.NewGuid() }, Command with { ExpectedItemRevision = 18 },
                Command with { CatalogVersion = 8 }, Command with { RequestId = Guid.NewGuid() } })
                Check(RidingRegistrationRules.Fingerprint(Actor, cmd) != fingerprint, "All command semantics are bound");
            Check(RidingRegistrationRules.Fingerprint(Actor with { CharacterId = Guid.NewGuid() }, Command) != fingerprint, "Character scope in fingerprint");
            Check(RidingRegistrationRules.Fingerprint(Actor with { RosterId = Guid.NewGuid() }, Command) != fingerprint, "World roster scope in fingerprint");
            var child = RidingRegistrationRules.ChildItemRequestId(Command.RequestId);
            Check(child != Command.RequestId && child == RidingRegistrationRules.ChildItemRequestId(Command.RequestId), "Stable separate child request");
            Check(child.ToString("D") == "20b4b6db-f1d6-8c41-9c73-ba1ef25382b0", "Independent child UUID reference vector");
            Check(child.ToString("D")[14] == '8' && "89ab".Contains(child.ToString("D")[19]), "UUIDv8 network-order layout");
            Check(child != RidingRegistrationRules.ChildItemRequestId(Guid.NewGuid()), "Different parent uses different child");
            var culture = CultureInfo.CurrentCulture;
            try { CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("tr-TR"); Check(fingerprint == RidingRegistrationRules.Fingerprint(Actor, Command), "Fingerprint culture invariant"); }
            finally { CultureInfo.CurrentCulture = culture; }
        });
    }
}
