using System.Security.Cryptography;
using System.Text;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Riding;

public sealed record RidingRegistrationActor(Guid CharacterId, Guid RosterId);
public sealed record RidingRegistrationCommand(Guid RequestId, Guid ItemId, long ExpectedItemRevision, int CatalogVersion);
public enum RidingRegistrationOutcome { Registered, AlreadyOwned }
public sealed record RidingUnlock(string MountId, int DefinitionVersion, DateTimeOffset UnlockedAtUtc, Guid SourceRequestId);

/// <summary>Transaction proposal only. No mutation, receipt persistence, authorization, or HTTP side effects.</summary>
public sealed record RidingRegistrationPlan(Guid RequestId, string Fingerprint, RidingRegistrationOutcome Outcome,
    Guid RosterId, long ExpectedRosterRevision, long NewRosterRevision, string MountId,
    Guid? ChildItemRequestId, ItemRecordMutation? Consumption, RidingUnlock? Unlock);

public static class RidingRegistrationRules
{
    public const string Operation = "Riding.Register";
    public const string ChildNamespace = "Riding.Register/v1";

    public static string Fingerprint(RidingRegistrationActor actor, RidingRegistrationCommand command)
    {
        Validate(actor, command);
        return RidingIdentity.Sha256(FormattableString.Invariant(
            $"ProjectRPG.Riding.Register/v1\ncharacter={actor.CharacterId:D}\nroster={actor.RosterId:D}\nrequest={command.RequestId:D}\nitem={command.ItemId:D}\nrevision={command.ExpectedItemRevision}\ncatalog={command.CatalogVersion}\n"));
    }

    // Name-derived UUIDv8: SHA-256 of namespace + LF + lowercase parent UUID, first 16 bytes in network order.
    public static Guid ChildItemRequestId(Guid parent)
    {
        if (parent == Guid.Empty) throw new RidingRuleException(RidingFailure.InvalidRequest);
        var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(ChildNamespace + "\n" + parent.ToString("D")));
        bytes[6] = (byte)((bytes[6] & 0x0f) | 0x80); bytes[8] = (byte)((bytes[8] & 0x3f) | 0x80);
        return Guid.ParseExact(Convert.ToHexString(bytes.AsSpan(0, 16)), "N");
    }

    /// <summary>Caller must hold the real account/character/lobby/roster/item locks and check receipts first.
    /// A successful plan must commit consume + unlock + parent/child receipts in ONE transaction.</summary>
    public static RidingRegistrationPlan Plan(RidingRegistrationActor actor, RidingRegistrationCommand command,
        RidingCatalog catalog, ItemRecord item, long rosterRevision, IReadOnlySet<string> ownedMountIds,
        bool hasActiveItemReservation, DateTimeOffset lockedAt)
    {
        string fingerprint = Fingerprint(actor, command);
        if (catalog is null || command.CatalogVersion != catalog.CatalogVersion) throw new RidingRuleException(RidingFailure.DefinitionMismatch);
        if (rosterRevision < 0 || ownedMountIds is null || ownedMountIds.Count > 512
            || ownedMountIds.Any(id => !RidingIdentity.Identifier(id, 64, true))) throw new RidingRuleException(RidingFailure.InvalidRequest);
        if (!WellFormed(item) || !ItemRecordRules.TryValidateRecord(item, out _)) throw new RidingRuleException(RidingFailure.ItemUnavailable);
        string character = actor.CharacterId.ToString("D");
        if (item.ItemId != command.ItemId || item.Owner != new ItemOwnerRef(ItemOwnerType.Character, character)
            || item.Location.ContainerType != ItemContainerType.Inventory || item.Location.ContainerId != character || !InventoryBagLayout.CanStore(item))
            throw new RidingRuleException(RidingFailure.WrongInventory);
        if (item.Revision != command.ExpectedItemRevision) throw new RidingRuleException(RidingFailure.RevisionConflict);
        if (hasActiveItemReservation) throw new RidingRuleException(RidingFailure.AssetReserved);
        if (item.Metadata.IsLocked) throw new RidingRuleException(RidingFailure.ItemLocked);
        if (item.Metadata.ExpiresAtUtc is { } expiry && expiry <= lockedAt) throw new RidingRuleException(RidingFailure.ItemExpired);
        if (item.LifecycleState != ItemLifecycleState.Active || item.State.Quantity < 1 || item.State.EquipmentProgression is not null)
            throw new RidingRuleException(RidingFailure.ItemUnavailable);
        var definition = catalog.Registration(new(item.DefinitionType, item.DefinitionName, item.DefinitionVersion));
        var mount = catalog.Mount(definition.MountId);
        if (!mount.Enabled || item.State.Quantity > definition.MaximumStackSize) throw new RidingRuleException(RidingFailure.DefinitionMismatch);

        // New duplicate requests still require a valid current inventory item. Receipt replay precedes this policy at the store boundary.
        if (ownedMountIds.Contains(mount.MountId))
            return new(command.RequestId, fingerprint, RidingRegistrationOutcome.AlreadyOwned, actor.RosterId,
                rosterRevision, rosterRevision, mount.MountId, null, null, null);
        if (rosterRevision == long.MaxValue) throw new RidingRuleException(RidingFailure.RosterRevisionOverflow);
        if (ownedMountIds.Count == 512) throw new RidingRuleException(RidingFailure.RosterLimitReached);

        var state = item.State with
        {
            Quantity = item.State.Quantity - 1,
            InstanceTags = Array.AsReadOnly(item.State.InstanceTags.ToArray()),
            StatValues = Array.AsReadOnly(item.State.StatValues.ToArray())
        };
        var consumed = item with { State = state };
        if (state.Quantity == 0) consumed = consumed with
        { LifecycleState = ItemLifecycleState.Consumed, Location = new(ItemContainerType.Terminal, "", -1) };
        if (!ItemRecordRules.TryValidateRecord(consumed, out _)) throw new RidingRuleException(RidingFailure.ItemUnavailable);
        return new(command.RequestId, fingerprint, RidingRegistrationOutcome.Registered, actor.RosterId,
            rosterRevision, rosterRevision + 1, mount.MountId, ChildItemRequestId(command.RequestId),
            new(command.ExpectedItemRevision, consumed), new(mount.MountId, mount.DefinitionVersion, lockedAt.ToUniversalTime(), command.RequestId));
    }

    private static void Validate(RidingRegistrationActor actor, RidingRegistrationCommand command)
    {
        if (actor is null || command is null || actor.CharacterId == Guid.Empty || actor.RosterId == Guid.Empty
            || command.RequestId == Guid.Empty || command.ItemId == Guid.Empty || command.ExpectedItemRevision <= 0
            || command.ExpectedItemRevision == long.MaxValue || command.CatalogVersion <= 0)
            throw new RidingRuleException(RidingFailure.InvalidRequest);
    }

    private static bool WellFormed(ItemRecord? item) => item is not null && item.Owner is not null && item.Location is not null
        && item.State is not null && item.Metadata is not null && item.Metadata.Durability is not null
        && item.Metadata.CreationSource is not null && item.State.InstanceTags is not null && item.State.StatValues is not null
        && item.State.EquipmentProgression is null && item.State.StatValues.All(s => s is not null) && item.Location.ContainerId is not null;
}
