using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class EquipmentRewardCases
{
    internal const string Target = "GameItem.Equipment.Progression.Target.EnduranceGreatSword";
    private const string Version = "RewardGrowthTest.v1";
    private sealed record Fixture(DungeonSession Session, string Server, Guid[] Characters);

    public static async Task RunAsync(bool postgres)
    {
        List<string> groups = [];
        if (!postgres)
        {
            InMemoryTransactionGate gate = new();
            InMemoryItemRepository items = new(gate);
            InMemoryGameRepository game = new(gate, items);
            InMemoryEconomyRepository economy = new(gate);
            InMemoryDungeonRewardCommitRepository rewards = new(gate, economy, items);
            DomainAndLegacy(groups);
            await WorkerCases(game, items, rewards, groups);
            await EquipmentRewardHttpCases.RunAsync(groups);
        }
        else
        {
            Require(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_RUN_ID"), "N", out _), "Missing owned run.");
            NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES"));
            Require(config.Host == "127.0.0.1" && config.Database == "equipment_test", "Refusing shared database.");
            await using NpgsqlDataSource data = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository items = new(data, NullLogger<PostgresItemRepository>.Instance);
            PostgresGameRepository game = new(data, NullLogger<PostgresGameRepository>.Instance, equipmentItems: items);
            PostgresEconomyRepository economy = new(data);
            PostgresDungeonRewardCommitRepository rewards = new(data, economy, items, NullLogger<PostgresDungeonRewardCommitRepository>.Instance);
            await game.EnsureCreatedAsync(default);
            await WorkerCases(game, items, rewards, groups);

            Fixture f = await NewFixture(game, 1);
            DungeonItemReward reward = Reward(2);
            var enqueued = await Enqueue(game, f, [reward]);
            await using NpgsqlDataSource fresh = NpgsqlDataSource.Create(config.ConnectionString);
            PostgresItemRepository freshItems = new(fresh, NullLogger<PostgresItemRepository>.Instance);
            PostgresGameRepository freshGame = new(fresh, NullLogger<PostgresGameRepository>.Instance, equipmentItems: freshItems);
            var restored = await freshGame.EnqueueDungeonRewardSettlementAsync(f.Session.DungeonSessionId, f.Server,
                Version, enqueued.CommandFingerprint, [], [reward], DateTimeOffset.UtcNow, default);
            Require(restored.ItemRewards.Single().EquipmentProgression == reward.EquipmentProgression, "JSONB settlement lost explicit progression.");
            PostgresDungeonRewardCommitRepository freshRewards = new(fresh, new(fresh), freshItems, NullLogger<PostgresDungeonRewardCommitRepository>.Instance);
            await Process(freshGame, freshRewards, f.Session.DungeonSessionId);
            Require((await freshItems.FindByOwnerAsync(Owner(f.Characters[0]), false, 100, default)).Single(x => x.DefinitionName == Target).State.EquipmentProgression?.Level == 2,
                "Fresh-pool worker/item reload lost growth.");
            groups.Add("postgres_fresh_pool_jsonb_settlement_worker_and_item_growth");
        }
        Console.WriteLine(JsonSerializer.Serialize(new { passed = true, scope = postgres ? "owned PostgreSQL reward growth" : "Memory worker, compatibility and actual backend HTTP", groups }));
    }

    private static void DomainAndLegacy(List<string> groups)
    {
        foreach (var definition in FirstSliceEquipmentDefinitions.All)
            for (int level = 0; level <= 3; level++)
                Require(DungeonRewardRules.TryValidateItemReward(Reward(level) with { DefinitionName = definition.DefinitionName }, out _), "Supported reward state rejected.");
        DungeonItemReward valid = Reward(0);
        DungeonItemReward[] invalid =
        [
            valid with { EquipmentProgression = new("Unknown", 1, 0) },
            valid with { EquipmentProgression = new("FirstSlice", 2, 0) },
            valid with { EquipmentProgression = new("FirstSlice", 1, -1) },
            valid with { EquipmentProgression = new("FirstSlice", 1, 4) },
            valid with { DefinitionName = StarterEquipment.DefinitionName },
            valid with { DefinitionName = FirstSliceEquipmentDefinitions.TokenDefinitionName },
            valid with { DefinitionType = "OtherDefinition" }, valid with { DefinitionVersion = 2 },
            valid with { Quantity = 2, MaxStackSize = 2 }, valid with { MaxStackSize = 0 },
            valid with { MaxStackSize = 999 }, valid with { BindState = ItemBindState.AccountBound }
        ];
        Require(invalid.All(x => !DungeonRewardRules.TryValidateItemReward(x, out _)), "Invalid growth reward accepted.");
        groups.Add("28_supported_states_and_12_invalid_reward_shapes");

        DungeonItemReward legacy = valid with { EquipmentProgression = null, MaxStackSize = 0 };
        string before = JsonSerializer.Serialize(new { legacy.DefinitionType, legacy.DefinitionName, legacy.DefinitionVersion,
            legacy.Quantity, legacy.BindState, legacy.DurabilityCurrent, legacy.DurabilityMaximum, legacy.InstanceTags, legacy.StatValues });
        string after = JsonSerializer.Serialize(legacy);
        Require(before == after && !after.Contains("EquipmentProgression")
            && JsonSerializer.Deserialize<DungeonItemReward>(before)!.EquipmentProgression is null, "Legacy JSON/fingerprint changed.");
        Guid id = Guid.NewGuid();
        string oldCanonical = JsonSerializer.Serialize(new { kind = "dungeon_reward", dungeonSessionId = id,
            rewardVersion = Version, changes = Array.Empty<CurrencyChange>(), itemRewards = new[] { JsonSerializer.Deserialize<JsonElement>(before) } });
        string newCanonical = JsonSerializer.Serialize(new { kind = "dungeon_reward", dungeonSessionId = id,
            rewardVersion = Version, changes = Array.Empty<CurrencyChange>(), itemRewards = new[] { legacy } });
        Require(oldCanonical == newCanonical && Fingerprint(id, [valid]) != Fingerprint(id, [legacy])
            && Fingerprint(id, [valid]) != Fingerprint(id, [valid with { EquipmentProgression = FirstSliceEquipmentProgression.Create() with { Level = 1 } }]),
            "Growth omitted from command identity or old fingerprint changed.");
        groups.Add("legacy_json_and_exact_fingerprint_unchanged_explicit_growth_changes_identity");
    }

    private static async Task WorkerCases(IGameRepository game, IItemRepository items, IDungeonRewardCommitRepository rewards, List<string> groups)
    {
        Fixture f = await NewFixture(game, 2);
        ItemOwnerRef fullOwner = Owner(f.Characters[0]);
        for (int start = 0; start < 80; start += 16)
        {
            var filler = Enumerable.Range(start, 16).Select(slot => EquipmentCraftingCases.Make(fullOwner, "GameItem.RewardFixture.Filler", slot, 1)).ToArray();
            Require((await items.CommitAsync(new(Guid.NewGuid(), "RewardGrowthFixture", "owned-fill", fullOwner, 0,
                filler.Select(x => new ItemRecordMutation(0, x)).ToArray()), DateTimeOffset.UtcNow, default)).Status == ItemRepositoryCommitStatus.Committed, "Full bag seed failed.");
        }
        DungeonItemReward[] grants = [Reward(0), Reward(3), Reward(0) with { EquipmentProgression = null, MaxStackSize = 0 }];
        var snapshot = await Enqueue(game, f, grants);
        try
        {
            await game.EnqueueDungeonRewardSettlementAsync(f.Session.DungeonSessionId, f.Server, Version,
                Fingerprint(f.Session.DungeonSessionId, [Reward(1), grants[1], grants[2]]), [], [Reward(1), grants[1], grants[2]], DateTimeOffset.UtcNow, default);
            throw new InvalidOperationException("Different growth reused a frozen settlement.");
        }
        catch (DungeonRewardSettlementConflictException) { }
        await Process(game, rewards, f.Session.DungeonSessionId);
        foreach (Guid character in f.Characters)
        {
            var delivered = (await items.FindByOwnerAsync(Owner(character), false, 200, default)).Where(x => x.DefinitionName == Target).ToArray();
            Require(delivered.Length == 3 && delivered.Select(x => x.ItemId).Distinct().Count() == 3
                && delivered.Count(x => x.State.EquipmentProgression?.Level == 0) == 1
                && delivered.Count(x => x.State.EquipmentProgression?.Level == 3) == 1
                && delivered.Count(x => x.State.EquipmentProgression is null) == 1, "Worker lost/reset/merged explicit or legacy growth.");
            Require(delivered.All(x => x.State.Quantity == 1 && x.Revision == 1 && x.Metadata.BindState == ItemBindState.CharacterBound), "Reward record invariants changed.");
            Require(character == f.Characters[0] ? delivered.All(x => x.Location.ContainerType == ItemContainerType.Mail)
                : delivered.Count(x => x.Location.ContainerType == ItemContainerType.Inventory) == 2
                    && delivered.Single(x => x.State.EquipmentProgression is null).Location.ContainerType == ItemContainerType.Mail, "Member-specific inventory/mail delivery changed.");
        }
        var replay = await Enqueue(game, f, grants);
        Require(replay.State == DungeonRewardSettlementState.Completed
            && JsonSerializer.Serialize(replay.ItemRewards) == JsonSerializer.Serialize(snapshot.ItemRewards), "Frozen successful snapshot changed.");
        groups.Add("two_member_worker_preserves_explicit_levels_and_distinct_items_inventory_or_mail");
        groups.Add("legacy_missing_growth_stays_null_and_growth_change_conflicts_with_frozen_snapshot");

        Fixture retry = await NewFixture(game, 1);
        await Enqueue(game, retry, [Reward(0)]);
        LostResponseCommit lost = new(rewards);
        await Process(game, lost, retry.Session.DungeonSessionId);
        Require(lost.Calls >= 2 && lost.Batch is not null, "Worker did not retry the committed-but-lost response.");
        ItemRecord current = (await items.FindByOwnerAsync(Owner(retry.Characters[0]), false, 100, default)).Single(x => x.DefinitionName == Target);
        Require(current.State.EquipmentProgression?.Level == 0 && current.Revision == 1, "Worker retry duplicated/reset growth.");
        ItemRecord next = current with { Location = current.Location with { SlotIndex = 20 }, State = current.State with { EquipmentProgression = FirstSliceEquipmentProgression.Create() with { Level = 1 } } };
        Require((await items.CommitAsync(new(Guid.NewGuid(), "OwnedGrowthFixture", "owned-advance", current.Owner, 0,
            [new(current.Revision, next)]), DateTimeOffset.UtcNow, default)).Status == ItemRepositoryCommitStatus.Committed, "Post-reward mutation failed.");
        Require((await rewards.CommitAsync(lost.Batch!, DateTimeOffset.UtcNow, default)).Succeeded
            && (await items.FindAsync(current.ItemId, default)) is { Revision: 2, State.EquipmentProgression.Level: 1, Location.SlotIndex: 20 }
            && (await items.FindByOwnerAsync(current.Owner, false, 100, default)).Count(x => x.DefinitionName == Target) == 1,
            "Original reward replay overwrote a newer item or granted twice.");
        groups.Add("worker_retry_after_lost_commit_response_and_old_batch_replay_preserve_newer_growth");
    }

    private sealed class LostResponseCommit(IDungeonRewardCommitRepository inner) : IDungeonRewardCommitRepository
    {
        public int Calls { get; private set; }
        public DungeonRewardCommitBatch? Batch { get; private set; }
        public async Task<DungeonRewardCommitResult> CommitAsync(DungeonRewardCommitBatch batch, DateTimeOffset now, CancellationToken ct)
        {
            Calls++; Batch ??= batch;
            var result = await inner.CommitAsync(batch, now, ct);
            if (Calls == 1 && result.Succeeded) throw new IOException("Owned test: response lost after successful atomic commit.");
            return result;
        }
    }

    private static async Task Process(IGameRepository game, IDungeonRewardCommitRepository rewards, Guid id)
    {
        using var worker = new DungeonRewardSettlementWorker(game, rewards, TimeProvider.System,
            Options.Create(new DungeonSettlementOptions { PollIntervalMilliseconds = 50, ProcessingLeaseSeconds = 5, MaximumAttempts = 3, MaximumRetryDelaySeconds = 1 }),
            NullLogger<DungeonRewardSettlementWorker>.Instance);
        await worker.StartAsync(default);
        try
        {
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(15));
            while (true)
            {
                DungeonSession session = (await game.GetDungeonSessionAsync(id, DateTimeOffset.UtcNow, timeout.Token))!;
                if (session.State == DungeonSessionState.Cleared) break;
                Require(session.State == DungeonSessionState.SettlementPending, "Settlement failed: " + session.State);
                await Task.Delay(25, timeout.Token);
            }
        }
        finally { await worker.StopAsync(default); }
    }

    private static async Task<Fixture> NewFixture(IGameRepository game, int members)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        List<(string Steam, Guid Character)> characters = [];
        for (int i = 0; i < members; i++)
        {
            string suffix = Guid.NewGuid().ToString("N")[..10], steam = "rwg-" + suffix;
            await game.UpsertAccountAsync(new(steam, steam, false, false), now, default);
            var character = await game.CreateCharacterAsync(steam, "Rw" + suffix, now, default);
            characters.Add((steam, character.CharacterId));
        }
        var first = characters[0];
        DungeonSession session = await game.CreateDungeonSessionAsync(first.Steam, first.Character, "RewardGrowthFixture", "Normal", now, now.AddHours(1), default);
        foreach (var other in characters.Skip(1))
            await game.JoinDungeonSessionAsync(session.DungeonSessionId, other.Steam, other.Character, 4, now, now.AddHours(1), default);
        string server = "reward-growth-" + Guid.NewGuid().ToString("N");
        await game.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:7777", now, now.AddHours(1), default);
        await game.StartDungeonSessionAsync(session.DungeonSessionId, server, now, now.AddHours(1), default);
        return new(session, server, characters.Select(x => x.Character).ToArray());
    }
    private static Task<DungeonRewardSettlement> Enqueue(IGameRepository game, Fixture f, IReadOnlyList<DungeonItemReward> rewards) =>
        game.EnqueueDungeonRewardSettlementAsync(f.Session.DungeonSessionId, f.Server, Version, Fingerprint(f.Session.DungeonSessionId, rewards), [], rewards, DateTimeOffset.UtcNow, default);
    private static string Fingerprint(Guid dungeonSessionId, IReadOnlyList<DungeonItemReward> itemRewards) => Convert.ToHexStringLower(SHA256.HashData(
        Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new { kind = "dungeon_reward", dungeonSessionId, rewardVersion = Version, changes = Array.Empty<CurrencyChange>(), itemRewards }))));
    internal static DungeonItemReward Reward(int level) => new("RPGItemDefinition", Target, 1, 1, ItemBindState.CharacterBound,
        0, 0, [], [], 1, new("FirstSlice", 1, level));
    private static ItemOwnerRef Owner(Guid id) => new(ItemOwnerType.Character, id.ToString("D"));
    internal static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
