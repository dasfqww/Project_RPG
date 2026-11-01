using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class DungeonPersonalLootTests
{
    private const string Version = "personal_loot_v1";
    private static DungeonItemReward[] Table(int weight = 500) =>
    [
        new("RPGItemDefinition", FirstSliceEquipmentDefinitions.OreDefinitionName, 1, 4,
            ItemBindState.CharacterBound, 0, 0, [], [], 999),
        new("RPGItemDefinition", FirstSliceEquipmentDefinitions.TokenDefinitionName, 1, 2,
            ItemBindState.CharacterBound, 0, 0, [], [], 999),
        .. FirstSliceEquipmentDefinitions.All.Skip(3).Select(entry => new DungeonItemReward(
            "RPGItemDefinition", entry.DefinitionName, 1, 1, ItemBindState.CharacterBound,
            0, 0, [], [], 1, FirstSliceEquipmentProgression.Create(), weight))
    ];

    public static async Task Run()
    {
        Policy();
        await Http();
        InMemoryTransactionGate gate = new();
        InMemoryItemRepository items = new(gate);
        InMemoryEconomyRepository economy = new(gate);
        InMemoryGameRepository game = new();
        await Storage("Memory", game, game, items, new InMemoryDungeonRewardCommitRepository(gate, economy, items));
        await Postgres(required: false);
    }

    public static async Task Postgres(bool required)
    {
        string? connection = Environment.GetEnvironmentVariable("PROJECT_RPG_PERSONAL_LOOT_TEST_POSTGRES");
        if (string.IsNullOrWhiteSpace(connection))
        {
            Require(!required, "A dedicated PostgreSQL connection is required; this mode cannot skip.");
            Console.WriteLine("SKIP personal loot: PostgreSQL engine unavailable; no existing database used.");
            return;
        }
        await using NpgsqlDataSource before = NpgsqlDataSource.Create(connection!);
        await using NpgsqlDataSource after = NpgsqlDataSource.Create(connection!);
        PostgresGameRepository firstGame = new(before, NullLogger<PostgresGameRepository>.Instance);
        await firstGame.EnsureCreatedAsync(default);
        PostgresItemRepository pgItems = new(after, NullLogger<PostgresItemRepository>.Instance);
        PostgresEconomyRepository pgEconomy = new(after);
        await Storage("Postgres/new pool", firstGame,
            new PostgresGameRepository(after, NullLogger<PostgresGameRepository>.Instance), pgItems,
            new PostgresDungeonRewardCommitRepository(after, pgEconomy, pgItems, NullLogger<PostgresDungeonRewardCommitRepository>.Instance));
    }

    private static void Policy()
    {
        DungeonItemReward[] table = Table();
        int[] counts = new int[4];
        int none = 0;
        for (int bucket = 0; bucket < 10000; bucket++)
        {
            int[] chosen = DungeonPersonalLootSelection.SelectBucket(table, bucket);
            Require(chosen.Contains(0) && chosen.Contains(1) && chosen.Length is 2 or 3, "Guaranteed/exclusive selection changed.");
            if (chosen.Length == 2) none++; else counts[chosen[2] - 2]++;
        }
        Require(none == 8000 && counts.All(count => count == 500), "20% total/5% each exact bucket weights changed.");
        DungeonItemReward[] captured = DungeonPersonalLootSelection.Capture(table);
        string json = JsonSerializer.Serialize(captured);
        DungeonItemReward[] restored = JsonSerializer.Deserialize<DungeonItemReward[]>(json)!;
        Guid session = Guid.NewGuid();
        HashSet<string> outcomes = [];
        for (int index = 0; index < 100; index++)
        {
            Guid character = new(index, 0, 0, new byte[8]);
            int[] first = DungeonPersonalLootSelection.SelectIndices(session, character, Version, captured);
            Require(first.SequenceEqual(DungeonPersonalLootSelection.SelectIndices(session, character, Version, restored)), "Persisted seed changed selection.");
            outcomes.Add(string.Join(',', first));
        }
        Require(outcomes.Count > 1, "Selection was shared across recipients instead of keyed by character.");
        Require(captured.Where(item => item.PersonalLootWeight.HasValue).Select(item => item.PersonalLootSeed).Distinct().Count() == 1
            && captured[2].PersonalLootSeed != DungeonPersonalLootSelection.Capture(table)[2].PersonalLootSeed, "Seed capture is not settlement-local.");
        Require(!DungeonPersonalLootSelection.IsValidPool(Table(3000))
            && !DungeonPersonalLootSelection.IsValidPool([table[2] with { PersonalLootWeight = 0 }])
            && !DungeonPersonalLootSelection.IsValidPool([table[2] with { MaxStackSize = 0 }])
            && !DungeonPersonalLootSelection.IsValidPool([table[2] with { PersonalLootWeight = -1 }]), "Invalid pool was allowed.");
        bool invalid = false;
        try { DungeonPersonalLootSelection.SelectIndices(session, Guid.NewGuid(), Version, table); }
        catch (InvalidDataException) { invalid = true; }
        Require(invalid, "Missing seed silently rerolled or granted all choices.");
        invalid = false;
        try { DungeonPersonalLootSelection.Capture(captured); } catch (ArgumentException) { invalid = true; }
        Require(invalid, "Caller could choose a preexisting seed for a new job.");
        DungeonItemReward[] legacy = [table[0] with { MaxStackSize = 0 }];
        Require(!JsonSerializer.Serialize(DungeonPersonalLootSelection.Capture(legacy)).Contains("PersonalLoot")
            && DungeonPersonalLootSelection.SelectIndices(session, Guid.NewGuid(), Version, legacy).SequenceEqual([0]),
            "Old JSON/fixed-reward behavior changed.");
        Console.WriteLine("PASS personal loot: exact 10000 buckets, independent stable recipients, seed validation and legacy JSON");
    }

    public static async Task Http()
    {
        using ProfileHost host = new("Development");
        using HttpClient http = new() { BaseAddress = await host.StartAsync(), Timeout = TimeSpan.FromSeconds(10) };
        async Task<JsonElement> Send(HttpMethod method, string path, object? body, string? token, params HttpStatusCode[] expected)
        {
            using HttpRequestMessage request = new(method, path);
            if (body is not null) request.Content = JsonContent.Create(body);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            using HttpResponseMessage response = await http.SendAsync(request);
            Require(expected.Contains(response.StatusCode), $"Unexpected personal loot HTTP {(int)response.StatusCode} at {path}.");
            string text = await response.Content.ReadAsStringAsync();
            Require(!text.Contains("personalLootSeed", StringComparison.OrdinalIgnoreCase), "HTTP leaked internal loot seed.");
            return text.Length > 0 ? JsonSerializer.Deserialize<JsonElement>(text) : default;
        }
        JsonElement login = await Send(HttpMethod.Post, "/api/auth/steam-ticket", new { ticket = "dev:76561198000002001" }, null, HttpStatusCode.OK);
        string token = login.GetProperty("accessToken").GetString()!;
        JsonElement character = await Send(HttpMethod.Post, "/api/characters", new
        {
            name = "Loot" + Guid.NewGuid().ToString("N")[..8], classId = "RPGPlayerClassDefinition:GreatSwordWarrior"
        }, token, HttpStatusCode.Created);
        string characterId = character.GetProperty("characterId").GetString()!;
        JsonElement session = await Send(HttpMethod.Post, "/api/dungeon-sessions", new
        {
            characterId, dungeonId = "Progression.Training", difficulty = "Normal"
        }, token, HttpStatusCode.Created);
        string id = session.GetProperty("dungeonSessionId").GetString()!;
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/departure", new
        {
            characterId, requestId = Guid.NewGuid(), expectedLobbyRevision = session.GetProperty("lobbyRevision").GetInt64()
        }, token, HttpStatusCode.OK);
        JsonElement assigned = await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/activate", new
        {
            serverId = "personal-http", serverAddress = "127.0.0.1:19971"
        }, host.AdminToken, HttpStatusCode.OK);
        string serverToken = assigned.GetProperty("gameServerAccessToken").GetString()!;
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/start", new { serverId = "personal-http" }, serverToken, HttpStatusCode.OK);
        object Body(int weight) => new
        {
            serverId = "personal-http", rewardVersion = Version, changes = Array.Empty<object>(),
            itemRewards = Table(weight).Select(reward => new
            {
                reward.DefinitionType, reward.DefinitionName, reward.DefinitionVersion, reward.Quantity,
                BindState = "CharacterBound", reward.DurabilityCurrent, reward.DurabilityMaximum,
                reward.InstanceTags, reward.StatValues, reward.MaxStackSize, reward.EquipmentProgression,
                reward.PersonalLootWeight
            }).ToArray()
        };
        string path = $"/api/dungeon-sessions/{id}/settle-rewards";
        await Send(HttpMethod.Post, path, Body(500), token, HttpStatusCode.Forbidden);
        foreach (int weight in new[] { 0, -1, 3000, 10001 })
            await Send(HttpMethod.Post, path, Body(weight), serverToken, HttpStatusCode.BadRequest);
        JsonElement unchanged = await Send(HttpMethod.Get, $"/api/dungeon-sessions/{id}", null, token, HttpStatusCode.OK);
        Require(unchanged.GetProperty("state").GetString() == "InProgress", "Rejected pool changed the session.");
        await Send(HttpMethod.Post, path, Body(500), serverToken, HttpStatusCode.Accepted);
        JsonElement ready = default;
        for (int attempt = 0; attempt < 100; attempt++)
        {
            ready = await Send(HttpMethod.Get, $"/api/dungeon-sessions/{id}/reward-snapshot/{characterId}", null,
                serverToken, HttpStatusCode.Accepted, HttpStatusCode.OK);
            if (ready.GetProperty("state").GetString() == "Ready") break;
            await Task.Delay(50);
        }
        Require(ready.GetProperty("state").GetString() == "Ready", "HTTP settlement did not complete.");
        JsonElement[] items = ready.GetProperty("items").EnumerateArray().ToArray();
        Require(items.Where(item => item.GetProperty("definitionName").GetString() == FirstSliceEquipmentDefinitions.OreDefinitionName)
            .Sum(item => item.GetProperty("state").GetProperty("quantity").GetInt32()) == 4, "HTTP fixed ore reward changed.");
        Require(items.Where(item => item.GetProperty("definitionName").GetString()!.Contains(".Target."))
            .Sum(item => item.GetProperty("state").GetProperty("quantity").GetInt32()) <= 1, "HTTP awarded competing choices together.");
        JsonElement counted = await Send(HttpMethod.Get, $"/api/dungeon-sessions/{id}/reward-snapshot/{characterId}?rewardVersion={Version}",
            null, serverToken, HttpStatusCode.OK);
        int targetQuantity = items.Where(item => item.GetProperty("definitionName").GetString()!.Contains(".Target."))
            .Sum(item => item.GetProperty("state").GetProperty("quantity").GetInt32());
        Require(counted.GetProperty("delivery").GetProperty("rewardVersion").GetString() == Version
            && counted.GetProperty("delivery").GetProperty("itemQuantity").GetInt32() == 6 + targetQuantity,
            "Reward notice counted unselected candidates instead of the committed receipt.");
        Require(!ready.TryGetProperty("delivery", out JsonElement legacyDelivery) || legacyDelivery.ValueKind == JsonValueKind.Null,
            "Legacy snapshot unexpectedly changed its delivery contract.");
        await Send(HttpMethod.Get, $"/api/dungeon-sessions/{id}/reward-snapshot/{characterId}?rewardVersion=bad%20version",
            null, serverToken, HttpStatusCode.BadRequest);
        Console.WriteLine("PASS personal loot: actual backend HTTP rejects invalid/player requests; worker delivers fixed items plus optional loot without seed exposure");
    }

    private static async Task Storage(string provider, IGameRepository game, IGameRepository reopened,
        IItemRepository items, IDungeonRewardCommitRepository commits)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        List<GameCharacter> characters = [];
        for (int index = 0; index < 4; index++)
        {
            string steam = (76561198000003000UL + (ulong)index).ToString(System.Globalization.CultureInfo.InvariantCulture);
            await game.UpsertAccountAsync(new(steam, steam, false, false), now, default);
            characters.Add(await game.CreateCharacterAsync(steam, "Loot" + Guid.NewGuid().ToString("N")[..8], now, default));
        }
        GameCharacter leader = characters[0];
        DungeonSession session = await game.CreateDungeonSessionAsync(leader.SteamId, leader.CharacterId,
            "Dungeon.PersonalLoot", "Normal", now, now.AddHours(1), default);
        foreach (GameCharacter member in characters.Skip(1))
            await game.JoinDungeonSessionAsync(session.DungeonSessionId, member.SteamId, member.CharacterId, 4, now, now.AddHours(1), default);
        await game.AssignDungeonServerAsync(session.DungeonSessionId, "personal-storage", "127.0.0.1:7777", now, now.AddHours(1), default);
        await game.StartDungeonSessionAsync(session.DungeonSessionId, "personal-storage", now, now.AddHours(1), default);
        await game.AbandonDungeonSessionAsync(session.DungeonSessionId, characters[3].SteamId, characters[3].CharacterId, now, default);
        ItemOwnerRef Owner(GameCharacter member) => new(ItemOwnerType.Character, member.CharacterId.ToString("D"));
        async Task<ItemRecord[]> Records(GameCharacter member) => (await items.FindByOwnerAsync(Owner(member), false, 500, default)).ToArray();
        async Task Seed(GameCharacter member, IEnumerable<ItemRecord> records)
        {
            foreach (ItemRecord[] chunk in records.Chunk(16))
            {
                var result = await items.CommitAsync(new(Guid.NewGuid(), "Create", Guid.NewGuid().ToString("N"), Owner(member),
                    chunk.Sum(record => record.State.Quantity), chunk.Select(record => new ItemRecordMutation(0, record)).ToArray()), now, default);
                Require(result.Status == ItemRepositoryCommitStatus.Committed, "Unable to seed owned inventory fixture.");
            }
        }
        ItemRecord Fixture(GameCharacter member, string name, int slot, int quantity) => new("RPGItemDefinition", name, 1,
            Owner(member), new(ItemContainerType.Inventory, member.CharacterId.ToString("D"), slot),
            new(Guid.NewGuid(), 0, quantity, [], []), 0, ItemLifecycleState.Active,
            new(ItemBindState.CharacterBound, new(0, 0), null, "PersonalLootFixture", false));
        var occupied = (await Records(leader)).Where(record => record.Location.ContainerType == ItemContainerType.Inventory)
            .Select(record => record.Location.SlotIndex).ToHashSet();
        await Seed(leader, Enumerable.Range(0, 80).Where(slot => !occupied.Contains(slot))
            .Select(slot => Fixture(leader, "PersonalLoot.Block", slot, 1)));
        var openSlots = Enumerable.Range(0, 80).Except((await Records(characters[1]))
            .Where(record => record.Location.ContainerType == ItemContainerType.Inventory).Select(record => record.Location.SlotIndex)).Take(2).ToArray();
        await Seed(characters[1], [Fixture(characters[1], FirstSliceEquipmentDefinitions.OreDefinitionName, openSlots[0], 996),
            Fixture(characters[1], FirstSliceEquipmentDefinitions.TokenDefinitionName, openSlots[1], 998)]);
        Dictionary<Guid, ItemRecord[]> before = [];
        foreach (GameCharacter member in characters) before.Add(member.CharacterId, await Records(member));
        // Policy/HTTP above test the authored 20% table. Force one of four
        // choices here so actual worker +0/overflow/merge checks never pass
        // vacuously because all three random recipients happened to get none.
        DungeonItemReward[] table = Table(2500);
        // The production API supplies a 64-character SHA256 fingerprint. A
        // shorter fixture is padded by PostgreSQL CHAR(64) and cannot replay.
        string fingerprint = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(Guid.NewGuid().ToByteArray()));
        Task<DungeonRewardSettlement> Enqueue(IGameRepository repository) => repository.EnqueueDungeonRewardSettlementAsync(
            session.DungeonSessionId, "personal-storage", Version, fingerprint, [], table, now, default);
        DungeonRewardSettlement captured = await Enqueue(game);
        Require(captured.CharacterIds.Count == 3 && !captured.CharacterIds.Contains(characters[3].CharacterId), "Departed member entered the loot recipients.");
        bool conflict = false;
        try { await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId, "personal-storage", Version,
            new string(fingerprint[0] == '0' ? '1' : '0', 64), [], table, now, default); }
        catch (DungeonRewardSettlementConflictException) { conflict = true; }
        Require(conflict, "An existing loot job accepted a changed command fingerprint.");
        DungeonRewardSettlement claim = (await game.ClaimNextDungeonRewardSettlementAsync("old-worker", now, now.AddMinutes(1), default))!;
        await game.RequeueDungeonRewardSettlementAsync(session.DungeonSessionId, "old-worker", "fixture worker restart", now, now, default);
        DungeonRewardSettlement restored = await Enqueue(reopened);
        Require(claim.ItemRewards[2].PersonalLootSeed == captured.ItemRewards[2].PersonalLootSeed
            && restored.ItemRewards[2].PersonalLootSeed == captured.ItemRewards[2].PersonalLootSeed,
            "Retry/new connection pool changed the captured seed.");
        Console.WriteLine($"PASS personal loot: {provider} captures a server seed once; immutable recipients and requeue/new pool preserve it");
        DropFirstCommittedReply fault = new(commits);
        using DungeonRewardSettlementWorker worker = new(reopened, fault, TimeProvider.System,
            Options.Create(new DungeonSettlementOptions { PollIntervalMilliseconds = 50, MaximumRetryDelaySeconds = 1 }),
            NullLogger<DungeonRewardSettlementWorker>.Instance);
        await worker.StartAsync(default);
        try
        {
            bool complete = false;
            for (int attempt = 0; attempt < 200; attempt++)
            {
                if ((await reopened.GetDungeonSessionAsync(session.DungeonSessionId, DateTimeOffset.UtcNow, default))?.State == DungeonSessionState.Cleared)
                { complete = true; break; }
                await Task.Delay(30);
            }
            Require(complete, "Personal loot worker did not complete after lost commit acknowledgement.");
        }
        finally { await worker.StopAsync(default); }
        Require(fault.Signatures.Count == 2 && fault.Signatures.Distinct().Count() == 1,
            "Lost commit acknowledgement changed per-recipient rewards, IDs or stack limits.");
        foreach (GameCharacter member in characters.Take(3))
        {
            ItemRecord[] after = await Records(member);
            int[] selected = DungeonPersonalLootSelection.SelectIndices(session.DungeonSessionId, member.CharacterId, Version, restored.ItemRewards);
            foreach (int index in selected)
            {
                DungeonItemReward reward = table[index];
                int Quantity(IEnumerable<ItemRecord> records) => records.Where(record => record.DefinitionName == reward.DefinitionName).Sum(record => record.State.Quantity);
                Require(Quantity(after) - Quantity(before[member.CharacterId]) == reward.Quantity, "Recipient received missing/duplicate/wrong selected reward.");
            }
            ItemRecord[] gear = after.Where(record => record.DefinitionName.Contains(".Target.")).ToArray();
            Require(gear.Length == selected.Count(index => index >= 2) && gear.All(record => record.State.EquipmentProgression == FirstSliceEquipmentProgression.Create()),
                "Exclusive +0 equipment result was not preserved.");
            if (member == leader) Require(after.Where(record => record.Metadata.CreationSource == "DungeonClear." + Version)
                .All(record => record.Location.ContainerType == ItemContainerType.Mail), "Full bag did not deliver selected rewards to mail.");
            else Require(after.Where(record => record.Metadata.CreationSource == "DungeonClear." + Version)
                .All(record => record.Location.ContainerType == ItemContainerType.Inventory), "Open bag unnecessarily received mail.");
        }
        Require((await Records(characters[3])).SequenceEqual(before[characters[3].CharacterId]), "Departed member received a reward.");
        Require((await Enqueue(reopened)).ItemRewards[2].PersonalLootSeed == captured.ItemRewards[2].PersonalLootSeed, "Completed replay changed loot seed.");
        Console.WriteLine($"PASS personal loot: {provider} actual worker retries one committed batch; fixed/selected +0 rewards, full-bag mail and partial stacks remain exact");
    }

    private sealed class DropFirstCommittedReply(IDungeonRewardCommitRepository inner) : IDungeonRewardCommitRepository
    {
        public List<string> Signatures { get; } = [];
        public async Task<DungeonRewardCommitResult> CommitAsync(DungeonRewardCommitBatch batch, DateTimeOffset now, CancellationToken token)
        {
            Signatures.Add(JsonSerializer.Serialize(batch));
            DungeonRewardCommitResult result = await inner.CommitAsync(batch, now, token);
            Require(result.Succeeded, "The personal loot atomic batch was rejected.");
            if (Signatures.Count == 1) throw new IOException("Owned fixture drops the first committed acknowledgement.");
            return result;
        }
    }

    private static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
