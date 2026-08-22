namespace ProjectRpg.Backend.Domain;

public enum ItemOwnerType
{
    None,
    Character,
    Account,
    System,
    World
}

public enum ItemContainerType
{
    None,
    Inventory,
    Equipment,
    CharacterStorage,
    AccountStorage,
    Mail,
    Trade,
    Auction,
    World,
    Terminal
}

public enum ItemBindState
{
    Unbound,
    BindOnEquip,
    CharacterBound,
    AccountBound
}

public enum ItemLifecycleState
{
    Active,
    Consumed,
    Destroyed,
    Expired
}

public enum ItemRepositoryCommitStatus
{
    Committed,
    AlreadyCommitted,
    InvalidRequest,
    IdempotencyConflict,
    NotFound,
    RevisionConflict,
    LocationConflict,
    ValidationFailed,
    InternalError
}

public sealed record ItemOwnerRef(
    ItemOwnerType Type,
    string OwnerId);

public sealed record ItemLocation(
    ItemContainerType ContainerType,
    string ContainerId,
    int SlotIndex);

public sealed record ItemDurability(
    int Current,
    int Maximum);

public sealed record ItemRecordMetadata(
    ItemBindState BindState,
    ItemDurability Durability,
    DateTimeOffset? ExpiresAtUtc,
    string CreationSource,
    bool IsLocked);

public sealed record ItemStatValue(
    string StatTag,
    double Value);

public sealed record ItemInstanceState(
    Guid InstanceId,
    int GenerationSeed,
    int Quantity,
    IReadOnlyList<string> InstanceTags,
    IReadOnlyList<ItemStatValue> StatValues);

public sealed record ItemRecord(
    string DefinitionType,
    string DefinitionName,
    int DefinitionVersion,
    ItemOwnerRef Owner,
    ItemLocation Location,
    ItemInstanceState State,
    long Revision,
    ItemLifecycleState LifecycleState,
    ItemRecordMetadata Metadata)
{
    public Guid ItemId => State.InstanceId;
}

public sealed record ItemRecordMutation(
    long ExpectedRevision,
    ItemRecord NewRecord);

public sealed record ItemRepositoryCommitRequest(
    Guid RequestId,
    string Operation,
    string CommandFingerprint,
    ItemOwnerRef Actor,
    int AffectedQuantity,
    IReadOnlyList<ItemRecordMutation> Mutations);

public sealed record ItemRepositoryCommitResult(
    ItemRepositoryCommitStatus Status,
    Guid RequestId,
    string Operation,
    string CommandFingerprint,
    ItemOwnerRef Actor,
    int AffectedQuantity,
    IReadOnlyList<ItemRecord> Records,
    DateTimeOffset CommittedAt);

public static class ItemRecordRules
{
    public const int MaximumMutationCount = 16;

    public static bool IsValidOwner(ItemOwnerRef owner)
    {
        return Enum.IsDefined(owner.Type)
            && owner.Type != ItemOwnerType.None
            && IsBoundedText(owner.OwnerId, 128);
    }

    public static bool TryValidateCommit(
        ItemRepositoryCommitRequest request,
        out string error)
    {
        if (request.RequestId == Guid.Empty)
        {
            error = "RequestId must be a non-empty UUID.";
            return false;
        }

        if (!IsSimpleIdentifier(request.Operation, 64))
        {
            error = "Operation must be a simple identifier of at most 64 characters.";
            return false;
        }

        if (!IsBoundedText(request.CommandFingerprint, 512))
        {
            error = "CommandFingerprint must contain at most 512 characters.";
            return false;
        }

        if (!IsValidOwner(request.Actor))
        {
            error = "Actor is invalid.";
            return false;
        }

        if (request.AffectedQuantity < 0)
        {
            error = "AffectedQuantity cannot be negative.";
            return false;
        }

        if (request.Mutations.Count is < 1 or > MaximumMutationCount)
        {
            error = $"A commit requires 1 to {MaximumMutationCount} mutations.";
            return false;
        }

        HashSet<Guid> itemIds = [];
        foreach (ItemRecordMutation mutation in request.Mutations)
        {
            if (mutation.ExpectedRevision < 0)
            {
                error = "ExpectedRevision cannot be negative.";
                return false;
            }

            if (mutation.NewRecord.Revision != mutation.ExpectedRevision)
            {
                error = "A mutation record must carry its expected revision.";
                return false;
            }

            if (!itemIds.Add(mutation.NewRecord.ItemId))
            {
                error = "A commit cannot mutate the same item more than once.";
                return false;
            }

            if (!TryValidateRecord(mutation.NewRecord, out error))
            {
                return false;
            }
        }

        error = string.Empty;
        return true;
    }

    public static bool TryValidateRecord(ItemRecord record, out string error)
    {
        if (record.ItemId == Guid.Empty)
        {
            error = "Item instance ID must be a non-empty UUID.";
            return false;
        }

        if (!IsBoundedText(record.DefinitionType, 64)
            || !IsBoundedText(record.DefinitionName, 128)
            || record.DefinitionVersion < 1
            || !IsValidOwner(record.Owner)
            || !Enum.IsDefined(record.Location.ContainerType)
            || !Enum.IsDefined(record.LifecycleState)
            || !Enum.IsDefined(record.Metadata.BindState)
            || record.Revision < 0)
        {
            error = "Item definition, owner, or revision is invalid.";
            return false;
        }

        if (record.Metadata.Durability.Maximum < 0
            || record.Metadata.Durability.Current < 0
            || record.Metadata.Durability.Current > record.Metadata.Durability.Maximum
            || record.Metadata.CreationSource.Length > 128)
        {
            error = "Item metadata is invalid.";
            return false;
        }

        if (record.State.InstanceTags.Count > 64
            || record.State.StatValues.Count > 128)
        {
            error = "Item instance metadata is too large.";
            return false;
        }

        HashSet<string> tags = new(StringComparer.Ordinal);
        foreach (string tag in record.State.InstanceTags)
        {
            if (!IsBoundedText(tag, 128) || !tags.Add(tag))
            {
                error = "Item instance tags are invalid or duplicated.";
                return false;
            }
        }

        HashSet<string> statTags = new(StringComparer.Ordinal);
        foreach (ItemStatValue statValue in record.State.StatValues)
        {
            if (!IsBoundedText(statValue.StatTag, 128)
                || !double.IsFinite(statValue.Value)
                || !statTags.Add(statValue.StatTag))
            {
                error = "Item stat values are invalid or duplicated.";
                return false;
            }
        }

        if (record.LifecycleState == ItemLifecycleState.Active)
        {
            if (record.State.Quantity <= 0
                || record.Location.ContainerType is
                    ItemContainerType.None or ItemContainerType.Terminal
                || !IsBoundedText(record.Location.ContainerId, 128)
                || record.Location.SlotIndex < 0)
            {
                error = "An active item requires a positive quantity and active location.";
                return false;
            }
        }
        else if (record.State.Quantity != 0
            || record.Location.ContainerType != ItemContainerType.Terminal
            || record.Location.ContainerId.Length != 0
            || record.Location.SlotIndex != -1)
        {
            error = "A terminal item requires zero quantity and the terminal location.";
            return false;
        }

        error = string.Empty;
        return true;
    }

    public static ItemRecord SnapshotWithRevision(ItemRecord record, long revision)
    {
        ItemInstanceState state = record.State with
        {
            InstanceTags = record.State.InstanceTags.ToArray(),
            StatValues = record.State.StatValues.ToArray()
        };
        return record with
        {
            State = state,
            Revision = revision
        };
    }

    private static bool IsBoundedText(string value, int maximumLength)
    {
        return !string.IsNullOrWhiteSpace(value)
            && value.Length <= maximumLength
            && !value.Any(char.IsControl);
    }

    private static bool IsSimpleIdentifier(string value, int maximumLength)
    {
        return IsBoundedText(value, maximumLength)
            && value.All(character =>
                char.IsAsciiLetterOrDigit(character)
                || character is '.' or '_' or '-');
    }
}
