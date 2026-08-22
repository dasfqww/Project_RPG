using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

public sealed class DungeonRewardSettlementWorker(
    IGameRepository gameRepository,
    IDungeonRewardCommitRepository rewardCommitRepository,
    TimeProvider timeProvider,
    IOptions<DungeonSettlementOptions> options,
    ILogger<DungeonRewardSettlementWorker> logger) : BackgroundService
{
    private readonly DungeonSettlementOptions _options = options.Value;
    private readonly string _workerId =
        $"{Environment.MachineName}-{Guid.NewGuid():N}";

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        TimeSpan idleDelay = TimeSpan.FromMilliseconds(Math.Clamp(
            _options.PollIntervalMilliseconds,
            50,
            10_000));
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                bool processed = await TryProcessNextAsync(stoppingToken);
                if (!processed)
                {
                    await Task.Delay(idleDelay, timeProvider, stoppingToken);
                }
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception exception)
            {
                logger.LogError(
                    exception,
                    "Dungeon reward settlement worker loop failed.");
                await Task.Delay(idleDelay, timeProvider, stoppingToken);
            }
        }
    }

    private async Task<bool> TryProcessNextAsync(
        CancellationToken cancellationToken)
    {
        DateTimeOffset now = timeProvider.GetUtcNow();
        int leaseSeconds = Math.Clamp(
            _options.ProcessingLeaseSeconds,
            5,
            5 * 60);
        DungeonRewardSettlement? settlement =
            await gameRepository.ClaimNextDungeonRewardSettlementAsync(
                _workerId,
                now,
                now.AddSeconds(leaseSeconds),
                cancellationToken);
        if (settlement is null)
        {
            return false;
        }

        try
        {
            DungeonRewardCommitBatch batch = await BuildCommitBatchAsync(
                settlement,
                cancellationToken);
            DungeonRewardCommitResult commitResult =
                await rewardCommitRepository.CommitAsync(
                    batch,
                    timeProvider.GetUtcNow(),
                    cancellationToken);
            if (!commitResult.Succeeded)
            {
                await gameRepository.FailDungeonRewardSettlementAsync(
                    settlement.DungeonSessionId,
                    _workerId,
                    commitResult.Error ?? "Reward settlement failed permanently.",
                    timeProvider.GetUtcNow(),
                    cancellationToken);
                logger.LogError(
                    "Dungeon reward settlement {DungeonSessionId} failed permanently: {Error}",
                    settlement.DungeonSessionId,
                    commitResult.Error);
                return true;
            }

            await gameRepository.CompleteDungeonRewardSettlementAsync(
                settlement.DungeonSessionId,
                _workerId,
                timeProvider.GetUtcNow(),
                cancellationToken);
            logger.LogInformation(
                "Dungeon reward settlement {DungeonSessionId} completed for {MemberCount} members.",
                settlement.DungeonSessionId,
                settlement.CharacterIds.Count);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (PermanentSettlementException exception)
        {
            await gameRepository.FailDungeonRewardSettlementAsync(
                settlement.DungeonSessionId,
                _workerId,
                exception.Message,
                timeProvider.GetUtcNow(),
                cancellationToken);
            logger.LogError(
                exception,
                "Dungeon reward settlement {DungeonSessionId} failed permanently.",
                settlement.DungeonSessionId);
        }
        catch (Exception exception)
        {
            int maximumAttempts = Math.Clamp(
                _options.MaximumAttempts,
                1,
                100);
            DateTimeOffset retryNow = timeProvider.GetUtcNow();
            if (settlement.AttemptCount >= maximumAttempts)
            {
                await gameRepository.FailDungeonRewardSettlementAsync(
                    settlement.DungeonSessionId,
                    _workerId,
                    $"Retry limit reached: {exception.Message}",
                    retryNow,
                    cancellationToken);
                logger.LogError(
                    exception,
                    "Dungeon reward settlement {DungeonSessionId} exhausted retries.",
                    settlement.DungeonSessionId);
            }
            else
            {
                int maximumDelay = Math.Clamp(
                    _options.MaximumRetryDelaySeconds,
                    1,
                    10 * 60);
                int delaySeconds = Math.Min(
                    maximumDelay,
                    1 << Math.Min(settlement.AttemptCount - 1, 10));
                await gameRepository.RequeueDungeonRewardSettlementAsync(
                    settlement.DungeonSessionId,
                    _workerId,
                    exception.Message,
                    retryNow,
                    retryNow.AddSeconds(delaySeconds),
                    cancellationToken);
                logger.LogWarning(
                    exception,
                    "Dungeon reward settlement {DungeonSessionId} will retry after {DelaySeconds} seconds.",
                    settlement.DungeonSessionId,
                    delaySeconds);
            }
        }

        return true;
    }

    private async Task<DungeonRewardCommitBatch> BuildCommitBatchAsync(
        DungeonRewardSettlement settlement,
        CancellationToken cancellationToken)
    {
        List<CurrencyBatchEntry> currencyEntries = [];
        List<ItemRepositoryCommitRequest> itemRequests = [];
        foreach (Guid characterId in settlement.CharacterIds.Order())
        {
            CharacterEconomyContext? context =
                await gameRepository.GetCharacterEconomyContextAsync(
                    characterId,
                    cancellationToken);
            if (context is null)
            {
                throw new PermanentSettlementException(
                    $"Settlement member '{characterId}' no longer exists.");
            }

            if (settlement.Changes.Count > 0)
            {
                string commandFingerprint =
                    $"{settlement.CommandFingerprint}:{characterId:D}";
                CurrencyTransactionRequest request = new(
                    BuildDeterministicId(
                        "dungeon_reward",
                        settlement,
                        characterId),
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
                itemRequests.Add(BuildItemRequest(settlement, characterId));
            }
        }

        return new DungeonRewardCommitBatch(currencyEntries, itemRequests);
    }

    private static ItemRepositoryCommitRequest BuildItemRequest(
        DungeonRewardSettlement settlement,
        Guid characterId)
    {
        ItemOwnerRef owner = new(
            ItemOwnerType.Character,
            characterId.ToString("D"));
        string containerId = $"DungeonReward.{settlement.DungeonSessionId:N}";
        List<ItemRecordMutation> mutations = [];
        int affectedQuantity = 0;
        for (int index = 0; index < settlement.ItemRewards.Count; index++)
        {
            DungeonItemReward reward = settlement.ItemRewards[index];
            affectedQuantity = checked(affectedQuantity + reward.Quantity);
            byte[] itemHash = BuildHash(
                $"dungeon_reward_item|{settlement.DungeonSessionId:D}|{characterId:D}|{settlement.RewardVersion}|{index}");
            Guid itemId = GuidFromHash(itemHash);
            int generationSeed =
                BinaryPrimitives.ReadInt32LittleEndian(itemHash.AsSpan(16, 4))
                & int.MaxValue;
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
                    reward.StatValues.ToArray()),
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
            BuildDeterministicId(
                "dungeon_item_reward",
                settlement,
                characterId),
            "DungeonReward",
            $"{settlement.CommandFingerprint}:{characterId:D}:items",
            owner,
            affectedQuantity,
            mutations);
    }

    private static Guid BuildDeterministicId(
        string kind,
        DungeonRewardSettlement settlement,
        Guid characterId)
    {
        string source =
            $"{kind}|{settlement.DungeonSessionId:D}|{characterId:D}|{settlement.RewardVersion}";
        return GuidFromHash(BuildHash(source));
    }

    private static byte[] BuildHash(string source)
    {
        return SHA256.HashData(Encoding.UTF8.GetBytes(source));
    }

    private static Guid GuidFromHash(byte[] hash)
    {
        Span<byte> bytes = stackalloc byte[16];
        hash.AsSpan(0, 16).CopyTo(bytes);
        bytes[0] |= 1;
        return new Guid(bytes);
    }

    private sealed class PermanentSettlementException(string message)
        : Exception(message);
}
