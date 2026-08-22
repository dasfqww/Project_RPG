using System.Text.Json.Serialization;

namespace ProjectRpg.Backend.Contracts;

public sealed record SteamTicketRequest(
    [property: JsonPropertyName("ticket")] string Ticket);

public sealed record SteamTicketResponse(
    string AccessToken,
    DateTimeOffset ExpiresAt,
    string SteamId);

public sealed record CreateCharacterRequest(string Name);

public sealed record CharacterResponse(
    Guid CharacterId,
    Guid RosterId,
    string Name,
    DateTimeOffset CreatedAt);

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
    DateTimeOffset LeaseExpiresAt);

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
    DateTimeOffset? GameServerAccessTokenExpiresAt = null);

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
    IReadOnlyList<ItemStatValueContract?>? StatValues);

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
    string ServerId);

public sealed record ConsumeJoinTicketResponse(
    Guid DungeonSessionId,
    Guid CharacterId,
    string SteamId);

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
