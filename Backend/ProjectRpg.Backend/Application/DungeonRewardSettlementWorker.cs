using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

public sealed class DungeonRewardSettlementWorker(
    IDungeonSettlementRepository gameRepository,
    IDungeonRewardCommitRepository rewardCommitRepository,
    TimeProvider timeProvider,
    IOptions<DungeonSettlementOptions> options,
    ILogger<DungeonRewardSettlementWorker> logger,
    IDungeonSettlementObserver? observer = null) : BackgroundService
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

        using var attempt = new System.Diagnostics.Activity("DungeonRewardSettlement.Attempt")
            .SetIdFormat(System.Diagnostics.ActivityIdFormat.W3C).Start();
        Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.Started));
        try
        {
            DungeonRewardCommitBatch batch = await DungeonRewardCommitPlanner.BuildAsync(
                settlement,
                gameRepository,
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
                Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.Failed));
                logger.LogError(
                    "Dungeon reward settlement {DungeonSessionId} failed permanently: {Error}",
                    settlement.DungeonSessionId,
                    commitResult.Error);
                return true;
            }

            foreach (var entry in batch.CurrencyEntries)
                Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.ReceiptCommitted,
                    entry.Request.CharacterId, entry.Request.RequestId));
            foreach (var request in batch.ItemRequests)
                if (Guid.TryParse(request.Actor.OwnerId, out Guid character))
                    Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.ReceiptCommitted,
                        character, request.RequestId));
            await gameRepository.CompleteDungeonRewardSettlementAsync(
                settlement.DungeonSessionId,
                _workerId,
                timeProvider.GetUtcNow(),
                cancellationToken);
            Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.Completed));
            logger.LogInformation(
                "Dungeon reward settlement {DungeonSessionId} completed for {MemberCount} members.",
                settlement.DungeonSessionId,
                settlement.CharacterIds.Count);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (DungeonRewardPlanningException exception)
        {
            await gameRepository.FailDungeonRewardSettlementAsync(
                settlement.DungeonSessionId,
                _workerId,
                exception.Message,
                timeProvider.GetUtcNow(),
                cancellationToken);
            Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.Failed));
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
                Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.Failed));
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
                Observe(new(settlement.DungeonSessionId, settlement.AttemptCount, DungeonSettlementPhase.Requeued));
                logger.LogWarning(
                    exception,
                    "Dungeon reward settlement {DungeonSessionId} will retry after {DelaySeconds} seconds.",
                    settlement.DungeonSessionId,
                    delaySeconds);
            }
        }

        return true;
    }

    private void Observe(DungeonSettlementObservation observation)
    {
        try { observer?.Observe(observation); }
        catch (Exception) { logger.LogWarning("Settlement observation unavailable; reward processing continues."); }
    }

    // Compatibility facade. Reward readers use the shared domain identity directly.
    public static Guid BuildItemRewardRequestId(Guid dungeonSessionId, Guid characterId, string rewardVersion)
        => DungeonRewardIdentity.ItemRequestId(dungeonSessionId, characterId, rewardVersion);
}
