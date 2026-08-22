using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

internal static class ItemApiContractMapper
{
    public static bool TryParseOwner(
        string? rawType,
        string? rawOwnerId,
        out ItemOwnerRef owner,
        out string error)
    {
        if (!Enum.TryParse(
                rawType,
                ignoreCase: true,
                out ItemOwnerType ownerType))
        {
            owner = new ItemOwnerRef(
                ItemOwnerType.None,
                string.Empty);
            error = "Owner type is invalid.";
            return false;
        }

        string ownerId = rawOwnerId?.Trim() ?? string.Empty;
        if (ownerType == ItemOwnerType.Character)
        {
            if (!Guid.TryParse(ownerId, out Guid characterId))
            {
                owner = new ItemOwnerRef(
                    ItemOwnerType.None,
                    string.Empty);
                error = "A character owner ID must be a UUID.";
                return false;
            }

            ownerId = characterId.ToString("D");
        }

        owner = new ItemOwnerRef(ownerType, ownerId);
        if (!ItemRecordRules.IsValidOwner(owner))
        {
            error = "Owner is invalid.";
            return false;
        }

        error = string.Empty;
        return true;
    }

    public static bool TryToDomain(
        ItemCommitRequestContract contract,
        out ItemRepositoryCommitRequest request,
        out string error)
    {
        if (contract.Actor is null || contract.Mutations is null)
        {
            request = EmptyRequest(contract.RequestId);
            error = "Actor and mutations are required.";
            return false;
        }

        if (!TryParseOwner(
            contract.Actor.Type,
            contract.Actor.OwnerId,
            out ItemOwnerRef actor,
            out error))
        {
            request = EmptyRequest(contract.RequestId);
            return false;
        }

        List<ItemRecordMutation> mutations = [];
        foreach (ItemRecordMutationContract? mutation in contract.Mutations)
        {
            if (mutation?.NewRecord is null
                || !TryToDomain(
                    mutation.NewRecord,
                    out ItemRecord record,
                    out error))
            {
                request = EmptyRequest(contract.RequestId);
                error = string.IsNullOrEmpty(error)
                    ? "Every mutation requires an item record."
                    : error;
                return false;
            }

            mutations.Add(new ItemRecordMutation(
                mutation.ExpectedRevision,
                record));
        }

        request = new ItemRepositoryCommitRequest(
            contract.RequestId,
            contract.Operation ?? string.Empty,
            contract.CommandFingerprint ?? string.Empty,
            actor,
            contract.AffectedQuantity,
            mutations);
        error = string.Empty;
        return true;
    }

    public static ItemCommitResponse ToContract(
        ItemRepositoryCommitResult result)
    {
        return new ItemCommitResponse(
            result.Status.ToString(),
            result.RequestId,
            result.Operation,
            result.CommandFingerprint,
            ToContract(result.Actor),
            result.AffectedQuantity,
            result.Records.Select(ToContract).ToArray(),
            result.CommittedAt);
    }

    public static ItemRecordContract ToContract(ItemRecord record)
    {
        return new ItemRecordContract(
            record.DefinitionType,
            record.DefinitionName,
            record.DefinitionVersion,
            ToContract(record.Owner),
            new ItemLocationContract(
                record.Location.ContainerType.ToString(),
                record.Location.ContainerId,
                record.Location.SlotIndex),
            new ItemInstanceStateContract(
                record.State.InstanceId,
                record.State.GenerationSeed,
                record.State.Quantity,
                record.State.InstanceTags
                    .Select(tag => (string?)tag)
                    .ToArray(),
                record.State.StatValues
                    .Select(stat => (ItemStatValueContract?)
                        new ItemStatValueContract(
                            stat.StatTag,
                            stat.Value))
                    .ToArray()),
            record.Revision,
            record.LifecycleState.ToString(),
            new ItemRecordMetadataContract(
                record.Metadata.BindState.ToString(),
                new ItemDurabilityContract(
                    record.Metadata.Durability.Current,
                    record.Metadata.Durability.Maximum),
                record.Metadata.ExpiresAtUtc,
                record.Metadata.CreationSource,
                record.Metadata.IsLocked));
    }

    private static bool TryToDomain(
        ItemRecordContract contract,
        out ItemRecord record,
        out string error)
    {
        if (contract.Owner is null
            || contract.Location is null
            || contract.State is null
            || contract.Metadata?.Durability is null)
        {
            record = EmptyRecord();
            error = "Item owner, location, state, metadata, and durability are required.";
            return false;
        }

        if (!TryParseOwner(
                contract.Owner.Type,
                contract.Owner.OwnerId,
                out ItemOwnerRef owner,
                out error)
            || !Enum.TryParse(
                contract.Location.ContainerType,
                ignoreCase: true,
                out ItemContainerType containerType)
            || !Enum.TryParse(
                contract.LifecycleState,
                ignoreCase: true,
                out ItemLifecycleState lifecycleState)
            || !Enum.TryParse(
                contract.Metadata.BindState,
                ignoreCase: true,
                out ItemBindState bindState))
        {
            record = EmptyRecord();
            error = string.IsNullOrEmpty(error)
                ? "Item location, lifecycle, or bind state is invalid."
                : error;
            return false;
        }

        if (contract.State.InstanceTags is null
            || contract.State.StatValues is null
            || contract.State.InstanceTags.Any(tag => tag is null)
            || contract.State.StatValues.Any(stat => stat?.StatTag is null))
        {
            record = EmptyRecord();
            error = "Item tags and stat values cannot contain null entries.";
            return false;
        }

        ItemInstanceState state = new(
            contract.State.InstanceId,
            contract.State.GenerationSeed,
            contract.State.Quantity,
            contract.State.InstanceTags.Select(tag => tag!).ToArray(),
            contract.State.StatValues
                .Select(stat => new ItemStatValue(
                    stat!.StatTag!,
                    stat.Value))
                .ToArray());
        ItemRecordMetadata metadata = new(
            bindState,
            new ItemDurability(
                contract.Metadata.Durability.Current,
                contract.Metadata.Durability.Maximum),
            contract.Metadata.ExpiresAtUtc,
            contract.Metadata.CreationSource ?? string.Empty,
            contract.Metadata.IsLocked);
        record = new ItemRecord(
            contract.DefinitionType ?? string.Empty,
            contract.DefinitionName ?? string.Empty,
            contract.DefinitionVersion,
            owner,
            new ItemLocation(
                containerType,
                contract.Location.ContainerId ?? string.Empty,
                contract.Location.SlotIndex),
            state,
            contract.Revision,
            lifecycleState,
            metadata);
        error = string.Empty;
        return true;
    }

    private static ItemOwnerContract ToContract(ItemOwnerRef owner)
    {
        return new ItemOwnerContract(
            owner.Type.ToString(),
            owner.OwnerId);
    }

    private static ItemRepositoryCommitRequest EmptyRequest(Guid requestId)
    {
        return new ItemRepositoryCommitRequest(
            requestId,
            string.Empty,
            string.Empty,
            new ItemOwnerRef(ItemOwnerType.None, string.Empty),
            0,
            []);
    }

    private static ItemRecord EmptyRecord()
    {
        return new ItemRecord(
            string.Empty,
            string.Empty,
            0,
            new ItemOwnerRef(ItemOwnerType.None, string.Empty),
            new ItemLocation(ItemContainerType.None, string.Empty, -1),
            new ItemInstanceState(Guid.Empty, 0, 0, [], []),
            0,
            ItemLifecycleState.Destroyed,
            new ItemRecordMetadata(
                ItemBindState.Unbound,
                new ItemDurability(0, 0),
                null,
                string.Empty,
                false));
    }
}
