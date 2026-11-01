using System.Text.Json.Serialization;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Contracts;

public sealed record SteamTicketRequest(
    [property: JsonPropertyName("ticket")] string Ticket);

public sealed record SteamTicketResponse(
    string AccessToken,
    DateTimeOffset ExpiresAt,
    string SteamId,
    DateTimeOffset IssuedAt);

public sealed record CreateCharacterRequest(string Name, string? ClassId = null);

public sealed record CharacterResponse(
    Guid CharacterId,
    Guid RosterId,
    string Name,
    DateTimeOffset CreatedAt,
    CharacterProgressionSummary? Progression = null);

public sealed record RosterResponse(
    Guid RosterId,
    string WorldId,
    DateTimeOffset CreatedAt);

public sealed record CreateDungeonSessionRequest(
    Guid CharacterId,
    string DungeonId,
    string Difficulty);

public sealed record JoinDungeonSessionRequest(
    Guid CharacterId);

public sealed record DungeonAbandonmentResponse(
    Guid DungeonSessionId,
    Guid CharacterId,
    DateTimeOffset AbandonedAt,
    string SessionStateAtAbandonment,
    bool RewardExcluded);

public sealed record DungeonMembershipSnapshotMemberResponse(
    Guid CharacterId,
    string SteamId,
    DateTimeOffset LeaseExpiresAt,
    string? CharacterName = null,
    DateTimeOffset? DisconnectedAt = null,
    DateTimeOffset? ReconnectExpiresAt = null,
    string? ConnectionGeneration = null,
    string? DisconnectedGeneration = null);

public sealed record DungeonMembershipSnapshotAbandonmentResponse(
    Guid CharacterId,
    string SteamId,
    DateTimeOffset AbandonedAt,
    string SessionStateAtAbandonment,
    bool RewardExcluded);

public sealed record DungeonDisconnectionExpulsionResponse(
    Guid CharacterId,
    string SteamId,
    DateTimeOffset DisconnectedAt,
    DateTimeOffset ExpiredAt,
    DateTimeOffset ProcessedAt,
    bool RewardExcluded);

public sealed record DungeonMembershipSnapshotResponse(
    Guid DungeonSessionId,
    string ServerId,
    string State,
    DateTimeOffset UpdatedAt,
    DateTimeOffset ExpiresAt,
    IReadOnlyList<DungeonMembershipSnapshotMemberResponse> Members,
    IReadOnlyList<DungeonMembershipSnapshotAbandonmentResponse> Abandonments,
    IReadOnlyList<DungeonDisconnectionExpulsionResponse> DisconnectionExpulsions,
    DateTimeOffset? ObservedAt = null);

public sealed record AssignDungeonServerRequest(
    string ServerId,
    string ServerAddress);

public sealed record ClaimDungeonServerRequest(
    string ServerId,
    string ServerAddress);

public sealed record DungeonSessionServerRequest(
    string ServerId);

public sealed record FinishDungeonSessionRequest(
    string ServerId,
    string Outcome);

public sealed record DungeonSessionMemberResponse(
    Guid CharacterId,
    DateTimeOffset JoinedAt,
    DateTimeOffset LeaseExpiresAt,
    string? CharacterName = null);

public sealed record DungeonSessionResponse(
    Guid DungeonSessionId,
    string DungeonId,
    string Difficulty,
    string State,
    string? ServerId,
    string? ServerAddress,
    DateTimeOffset ExpiresAt,
    IReadOnlyList<DungeonSessionMemberResponse> Members,
    string? GameServerAccessToken = null,
    DateTimeOffset? GameServerAccessTokenExpiresAt = null,
    string? ServerMapPackage = null,
    Guid? LeaderCharacterId = null,
    long LobbyRevision = 0,
    Guid? DepartureRequestId = null,
    long? DepartureRequestRevision = null,
    DateTimeOffset? DepartureRequestedAt = null);

public sealed record DungeonDepartureRequest(Guid CharacterId, Guid RequestId, long ExpectedLobbyRevision);

public sealed record SettleDungeonRewardsRequest(
    string ServerId,
    string RewardVersion,
    IReadOnlyList<CurrencyChangeContract?>? Changes,
    IReadOnlyList<DungeonItemRewardContract?>? ItemRewards = null);

public sealed record DungeonItemRewardContract(
    string? DefinitionType,
    string? DefinitionName,
    int DefinitionVersion,
    int Quantity,
    string? BindState,
    int DurabilityCurrent,
    int DurabilityMaximum,
    IReadOnlyList<string?>? InstanceTags,
    IReadOnlyList<ItemStatValueContract?>? StatValues,
    int MaxStackSize = 0,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    ItemEquipmentProgressionContract? EquipmentProgression = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? PersonalLootWeight = null);

public sealed record DungeonRewardSettlementResponse(
    Guid DungeonSessionId,
    string State,
    string RewardVersion,
    int MemberCount,
    DateTimeOffset UpdatedAt);

public sealed record CreateJoinTicketRequest(
    Guid CharacterId,
    Guid DungeonSessionId);

public sealed record CreateJoinTicketResponse(
    Guid DungeonSessionId,
    Guid CharacterId,
    string JoinTicket,
    DateTimeOffset ExpiresAt);

public sealed record ConsumeJoinTicketRequest(
    string JoinTicket,
    string ServerId,
    Guid? ConsumptionRequestId = null);

public sealed record ConsumeJoinTicketResponse(
    Guid DungeonSessionId,
    Guid CharacterId,
    string SteamId,
    long ConnectionGeneration);

public sealed record ReadJoinTicketConsumptionRequest(
    Guid DungeonSessionId,
    string ServerId,
    Guid ConsumptionRequestId,
    string TicketHash,
    string ExpectedSteamId);

// Deliberately distinct from ConsumeJoinTicketResponse; never an admission grant.
public sealed record ReadJoinTicketConsumptionResponse(
    string State,
    JoinTicketConsumptionEvidence Consumption);

public sealed record SealJoinTicketConsumptionRequest(
    Guid DungeonSessionId,
    string ServerId,
    Guid ConsumptionRequestId,
    string TicketHash,
    string ExpectedSteamId);

public sealed record ConfirmDungeonAdmissionRequest(
    string SteamId,
    long ConnectionGeneration);

public sealed record RecordDungeonDisconnectionRequest(
    string SteamId,
    long ConnectionGeneration,
    DateTimeOffset? ObservedAtUtc = null);

public sealed record InventoryItemContract(
    [property: JsonPropertyName("item_id")] string ItemId,
    [property: JsonPropertyName("quantity")] int Quantity,
    [property: JsonPropertyName("slot_index")] int SlotIndex,
    [property: JsonPropertyName("category")] string Category,
    [property: JsonPropertyName("instance_id")] string InstanceId);

public sealed record SaveInventoryRequest(
    [property: JsonPropertyName("characterId")] Guid CharacterId,
    [property: JsonPropertyName("inventory")] IReadOnlyList<InventoryItemContract> Inventory);

public sealed record LoadInventoryResponse(
    [property: JsonPropertyName("inventory")] IReadOnlyList<InventoryItemContract> Inventory);
