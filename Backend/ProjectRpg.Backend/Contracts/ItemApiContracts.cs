namespace ProjectRpg.Backend.Contracts;

public sealed record ItemOwnerContract(
    string? Type,
    string? OwnerId);

public sealed record ItemLocationContract(
    string? ContainerType,
    string? ContainerId,
    int SlotIndex);

public sealed record ItemDurabilityContract(
    int Current,
    int Maximum);

public sealed record ItemRecordMetadataContract(
    string? BindState,
    ItemDurabilityContract? Durability,
    DateTimeOffset? ExpiresAtUtc,
    string? CreationSource,
    bool IsLocked);

public sealed record ItemStatValueContract(
    string? StatTag,
    double Value);

public sealed record ItemInstanceStateContract(
    Guid InstanceId,
    int GenerationSeed,
    int Quantity,
    IReadOnlyList<string?>? InstanceTags,
    IReadOnlyList<ItemStatValueContract?>? StatValues);

public sealed record ItemRecordContract(
    string? DefinitionType,
    string? DefinitionName,
    int DefinitionVersion,
    ItemOwnerContract? Owner,
    ItemLocationContract? Location,
    ItemInstanceStateContract? State,
    long Revision,
    string? LifecycleState,
    ItemRecordMetadataContract? Metadata);

public sealed record ItemRecordMutationContract(
    long ExpectedRevision,
    ItemRecordContract? NewRecord);

public sealed record ItemCommitRequestContract(
    Guid RequestId,
    string? Operation,
    string? CommandFingerprint,
    ItemOwnerContract? Actor,
    int AffectedQuantity,
    IReadOnlyList<ItemRecordMutationContract?>? Mutations);

public sealed record ItemCommitResponse(
    string Status,
    Guid RequestId,
    string Operation,
    string CommandFingerprint,
    ItemOwnerContract Actor,
    int AffectedQuantity,
    IReadOnlyList<ItemRecordContract> Records,
    DateTimeOffset CommittedAt);

public sealed record LoadItemsResponse(
    IReadOnlyList<ItemRecordContract> Items);
