using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

/// <summary>Builds commands from a frozen settlement; inventory placement stays inside the atomic commit.</summary>
public static class DungeonRewardCommitPlanner
{
    public static async Task<DungeonRewardCommitBatch> BuildAsync(
        DungeonRewardSettlement settlement,
        ICharacterEconomyRepository characters,
        CancellationToken cancellationToken)
    {
        List<CurrencyBatchEntry> currencyEntries = [];
        List<ItemRepositoryCommitRequest> itemRequests = [];
        Dictionary<Guid, IReadOnlyList<int>> stackSizesByRequest = [];
        foreach (Guid characterId in settlement.CharacterIds.Order())
        {
            CharacterEconomyContext? context =
                await characters.GetCharacterEconomyContextAsync(
                    characterId,
                    cancellationToken);
            if (context is null)
            {
                throw new DungeonRewardPlanningException(
                    $"Settlement member '{characterId}' no longer exists.");
            }

            if (settlement.Changes.Count > 0)
            {
                string commandFingerprint =
                    $"{settlement.CommandFingerprint}:{characterId:D}";
                CurrencyTransactionRequest request = new(
                    DungeonRewardIdentity.CurrencyRequestId(settlement.DungeonSessionId, characterId, settlement.RewardVersion),
                    characterId,
                    settlement.DungeonSessionId,
                    "dungeon_reward",
                    commandFingerprint,
                    $"DungeonClear.{settlement.RewardVersion}",
                    settlement.Changes);
                currencyEntries.Add(new CurrencyBatchEntry(request, context));
            }

            if (settlement.ItemRewards.Count > 0)
            {
                int[] indices;
                try { indices = DungeonPersonalLootSelection.SelectIndices(settlement.DungeonSessionId,
                    characterId, settlement.RewardVersion, settlement.ItemRewards); }
                catch (Exception error) when (error is InvalidDataException or ArgumentException)
                { throw new DungeonRewardPlanningException("Invalid frozen personal loot selection."); }
                if (indices.Length > 0)
                {
                    ItemRepositoryCommitRequest request = BuildItemRequest(settlement, characterId, indices);
                    itemRequests.Add(request);
                    stackSizesByRequest.Add(request.RequestId, indices.Select(index => settlement.ItemRewards[index].MaxStackSize).ToArray());
                }
            }
        }

        return new DungeonRewardCommitBatch(
            currencyEntries,
            itemRequests,
            ItemMaxStackSizesByRequest: stackSizesByRequest);
    }

    private static ItemRepositoryCommitRequest BuildItemRequest(
        DungeonRewardSettlement settlement,
        Guid characterId,
        IReadOnlyList<int> indices)
    {
        ItemOwnerRef owner = new(
            ItemOwnerType.Character,
            characterId.ToString("D"));
        string containerId = $"DungeonReward.{settlement.DungeonSessionId:N}";
        List<ItemRecordMutation> mutations = [];
        int affectedQuantity = 0;
        foreach (int index in indices)
        {
            DungeonItemReward reward = settlement.ItemRewards[index];
            affectedQuantity = checked(affectedQuantity + reward.Quantity);
            (Guid itemId, int generationSeed) = DungeonRewardIdentity.ItemInstance(
                settlement.DungeonSessionId, characterId, settlement.RewardVersion, index);
            ItemRecord record = new(
                reward.DefinitionType,
                reward.DefinitionName,
                reward.DefinitionVersion,
                owner,
                new ItemLocation(ItemContainerType.Mail, containerId, index),
                new ItemInstanceState(
                    itemId,
                    generationSeed,
                    reward.Quantity,
                    reward.InstanceTags.ToArray(),
                    reward.StatValues.ToArray(),
                    reward.EquipmentProgression),
                0,
                ItemLifecycleState.Active,
                new ItemRecordMetadata(
                    reward.BindState,
                    new ItemDurability(
                        reward.DurabilityCurrent,
                        reward.DurabilityMaximum),
                    null,
                    $"DungeonClear.{settlement.RewardVersion}",
                    false));
            mutations.Add(new ItemRecordMutation(0, record));
        }

        return new ItemRepositoryCommitRequest(
            DungeonRewardIdentity.ItemRequestId(settlement.DungeonSessionId, characterId, settlement.RewardVersion),
            "DungeonReward",
            $"{settlement.CommandFingerprint}:{characterId:D}:items",
            owner,
            affectedQuantity,
            mutations);
    }
}

public sealed class DungeonRewardPlanningException(string message) : Exception(message);
