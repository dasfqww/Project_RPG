using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Contracts;

namespace ProjectRpg.Backend.Domain;

public enum DungeonPickupIntentStatus { Prepared, AlreadyPrepared, InvalidRequest, Forbidden, NotFound, Conflict }

// A reservation is NOT an item receipt and never authorizes a grant by itself.
public sealed record DungeonPickupIntent(Guid DungeonSessionId, string ServerId, Guid DropId,
    Guid RequestId, Guid CharacterId, string SteamId, Guid ItemId, string CommandJson, DateTimeOffset PreparedAt,
    DateTimeOffset? ConsumedAt = null);

public sealed record DungeonPickupIntentResult(DungeonPickupIntentStatus Status, DungeonPickupIntent? Intent = null);

public static class DungeonPickupIntentRules
{
    public static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public static bool ValidScope(Guid session, string server) => session != Guid.Empty
        && !string.IsNullOrWhiteSpace(server) && server.Length <= 128 && server == server.Trim()
        && !server.Any(char.IsControl);

    public static ItemCommitRequestContract ToContract(ItemRepositoryCommitRequest command) => new(
        command.RequestId, command.Operation, command.CommandFingerprint,
        new(command.Actor.Type.ToString(), command.Actor.OwnerId), command.AffectedQuantity,
        command.Mutations.Select(m => (ItemRecordMutationContract?)new ItemRecordMutationContract(
            m.ExpectedRevision, ItemApiContractMapper.ToContract(m.NewRecord))).ToArray());

    public static bool TryFreeze(Guid dropId, ItemRepositoryCommitRequest command, out string json)
    {
        json = string.Empty;
        if (dropId == Guid.Empty || command is null || command.Actor is null || command.Mutations is null
            || command.Mutations.Any(m => m is null || m.NewRecord is null)
            || !ItemRecordRules.TryValidateCommit(command, out _)
            || command.Operation != "Item.Pickup" || command.Actor.Type != ItemOwnerType.Character
            || !Guid.TryParse(command.Actor.OwnerId, out Guid character) || character == Guid.Empty
            || command.Actor.OwnerId != character.ToString("D") || command.Mutations.Count != 1
            || command.AffectedQuantity is < 1 or > 127) return false;
        ItemRecordMutation mutation = command.Mutations[0];
        ItemRecord record = mutation.NewRecord;
        if (mutation.ExpectedRevision != 0 || record.Revision != 0 || record.Owner != command.Actor
            || record.LifecycleState != ItemLifecycleState.Active || record.State.Quantity != command.AffectedQuantity
            || record.Location.ContainerType != ItemContainerType.Inventory
            || record.Location.ContainerId != command.Actor.OwnerId
            || record.Location.SlotIndex is < 0 or >= DungeonRewardDeliveryPlanner.InventorySlotCapacity
            || record.Metadata.CreationSource != "WorldPickup" || record.Metadata.IsLocked) return false;
        json = JsonSerializer.Serialize(ToContract(command), JsonOptions);
        return Encoding.UTF8.GetByteCount(json) <= 64 * 1024;
    }

    public static string Canonicalize(string json)
        => JsonSerializer.Serialize(ToContract(RestoreCommand(json)), JsonOptions);

    internal static ItemRepositoryCommitRequest RestoreCommand(DungeonPickupIntent intent)
    {
        var command = RestoreCommand(intent.CommandJson);
        if (command.RequestId != intent.RequestId || command.Actor.OwnerId != intent.CharacterId.ToString("D")
            || command.Mutations[0].NewRecord.ItemId != intent.ItemId)
            throw new InvalidOperationException("Persisted pickup keys did not match the frozen command.");
        return command;
    }

    public static ItemRepositoryCommitRequest RestoreCommand(string json)
    {
        var contract = JsonSerializer.Deserialize<ItemCommitRequestContract>(json, JsonOptions);
        if (contract is null || !ItemApiContractMapper.TryToDomain(contract, out var command, out _)
            || !TryFreeze(Guid.Parse("00000000-0000-0000-0000-000000000001"), command, out var canonical))
            throw new InvalidOperationException("Invalid persisted pickup command.");
        return command;
    }

    public static bool Same(DungeonPickupIntent intent, Guid session, string server, Guid drop,
        string commandJson) => intent.DungeonSessionId == session && intent.ServerId == server
        && intent.DropId == drop && intent.CommandJson == commandJson;
}
