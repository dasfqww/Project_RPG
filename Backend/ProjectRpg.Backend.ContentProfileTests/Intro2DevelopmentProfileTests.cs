using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Domain;

internal static class Intro2DevelopmentProfileTests
{
    public static async Task Run()
    {
        ConfigurationChecks();
        await Http();
        using ProfileHost production = new("Production", "Intro2");
        await production.ExpectRejectionAsync("Dungeon development profiles require Development or Testing.");
        Console.WriteLine("PASS Intro2 profile HTTP: Production rejects opt-in before listening");
    }

    private static ConfigurationManager Configuration(string name = "Intro2")
    {
        ConfigurationManager result = new();
        result.AddInMemoryCollection(new Dictionary<string, string?>
        {
            [DungeonDevelopmentProfiles.Setting] = name,
            ["DungeonContent:Entries:0:DungeonId"] = "Dungeon.RaidSwordMan",
            ["DungeonContent:Entries:0:MapPackage"] = "/Game/Maps/bossmap",
            ["DungeonContent:Entries:0:Difficulties:0"] = "Hard",
            ["DungeonContent:Entries:7:DungeonId"] = "MustNotMerge",
            ["DungeonSessions:MaxPartySize"] = "4", ["DungeonSessions:MinimumDeparturePartySize"] = "2",
            ["Steam:AllowDevelopmentAuthentication"] = "false", ["Storage:Provider"] = "Postgres"
        });
        return result;
    }

    private static void ConfigurationChecks()
    {
        foreach (string environment in new[] { "Development", "Testing" })
        {
            ConfigurationManager config = Configuration();
            DungeonContentCatalog catalog = DungeonDevelopmentProfiles.Resolve(config, new TestEnvironment(environment), AppContext.BaseDirectory);
            Require(catalog.AllowedSelections.Count == 3
                && catalog.GetMapPackage(FirstSliceProgression.Training, "Normal") == "/Game/Maps/Progression/L_ProgressionTraining"
                && catalog.GetMapPackage(FirstSliceProgression.Intro1, "Normal") == "/Game/Maps/Progression/L_ProgressionIntro1"
                && catalog.GetSelectionError(FirstSliceProgression.Intro1, "Hard") is not null
                && catalog.GetMapPackage(FirstSliceProgression.Intro2, "Normal") == "/Game/Maps/Progression/L_ProgressionIntro2"
                && catalog.GetSelectionError("Dungeon.RaidSwordMan", "Hard") is not null,
                "Catalog did not contain exactly the three Normal stages.");
            Require(config["DungeonSessions:MaxPartySize"] == "1" && config["DungeonSessions:MinimumDeparturePartySize"] == "1"
                && config["Steam:AllowDevelopmentAuthentication"] == "false" && config["Storage:Provider"] == "Postgres",
                "Profile changed security/storage settings or solo limits.");
        }
        Console.WriteLine("PASS Intro2 profile: exact Training+Intro1+Intro2/Normal snapshot, solo limits, security/storage preserved");
        foreach (string name in new[] { "intro2", "Intro2 ", "../Intro2", "Intro2.json" })
            Reject(() => DungeonDevelopmentProfiles.Resolve(Configuration(name), new TestEnvironment("Development"), "not-a-directory"));
        foreach (string environment in new[] { "Production", "Staging" })
            Reject(() => DungeonDevelopmentProfiles.Resolve(Configuration(), new TestEnvironment(environment), "not-a-directory"));
        Console.WriteLine("PASS Intro2 profile: case/path aliases and non-development environments fail closed");

        string fixture = Path.Combine(AppContext.BaseDirectory, "Intro2Fixture_" + Guid.NewGuid().ToString("N"));
        string path = Path.Combine(fixture, "DevelopmentProfiles", "Intro2.json");
        string packaged = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "DevelopmentProfiles", "Intro2.json"));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        try
        {
            Reject(() => DungeonDevelopmentProfiles.Resolve(Configuration(), new TestEnvironment("Development"), fixture));
            foreach (string invalid in new[] { "{}", packaged.Replace("\"name\": \"Intro2\"", "\"name\": \"Training\""),
                packaged.Replace("\"schemaVersion\": 1", "\"schemaVersion\": 2"),
                packaged.Replace("\"maximumPartySize\": 1", "\"maximumPartySize\": 5"),
                packaged.Replace("Progression.Intro1", "Progression.Training"),
                packaged.Replace("/Game/Maps/Progression/L_ProgressionIntro1", "/Game/Maps/Unsafe?game=Other") })
            {
                File.WriteAllText(path, invalid);
                ConfigurationManager config = Configuration();
                Reject(() => DungeonDevelopmentProfiles.Resolve(config, new TestEnvironment("Development"), fixture));
                Require(config["DungeonSessions:MaxPartySize"] == "4" && config["DungeonSessions:MinimumDeparturePartySize"] == "2",
                    "Invalid document partly applied the profile.");
            }
            File.WriteAllText(path, packaged);
            DungeonContentCatalog snapshot = DungeonDevelopmentProfiles.Resolve(Configuration(), new TestEnvironment("Development"), fixture);
            File.WriteAllText(path, "{}");
            Require(snapshot.AllowedSelections.Count == 3 && snapshot.GetSelectionError(FirstSliceProgression.Intro1, "Normal") is null,
                "Running catalog changed after the packaged file changed.");
        }
        finally
        {
            Require(Path.GetDirectoryName(fixture) == AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar)
                && !File.GetAttributes(fixture).HasFlag(FileAttributes.ReparsePoint), "Unsafe own fixture cleanup.");
            Directory.Delete(fixture, true);
        }
        Console.WriteLine("PASS Intro2 profile: missing/malformed documents are atomic; loaded catalog does not hot-reload");
    }

    private static async Task Http()
    {
        using ProfileHost host = new("Development", "Intro2");
        using HttpClient http = new() { BaseAddress = await host.StartAsync(), Timeout = TimeSpan.FromSeconds(10) };
        async Task<JsonElement> Send(HttpMethod method, string path, object? body, string? token, params HttpStatusCode[] expected)
        {
            using HttpRequestMessage request = new(method, path);
            if (body is not null) request.Content = JsonContent.Create(body);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            using HttpResponseMessage response = await http.SendAsync(request);
            Require(expected.Contains(response.StatusCode), $"Unexpected HTTP {(int)response.StatusCode} at {path}.");
            string text = await response.Content.ReadAsStringAsync();
            return text.Length == 0 ? default : JsonSerializer.Deserialize<JsonElement>(text);
        }
        JsonElement login = await Send(HttpMethod.Post, "/api/auth/steam-ticket", new { ticket = "dev:76561198000002101" }, null, HttpStatusCode.OK);
        string token = login.GetProperty("accessToken").GetString()!;
        async Task<string> Character(string suffix)
        {
            JsonElement result = await Send(HttpMethod.Post, "/api/characters", new
            {
                name = "Intro" + Guid.NewGuid().ToString("N")[..8] + suffix, classId = FirstSliceProgression.ClassId
            }, token, HttpStatusCode.Created);
            return result.GetProperty("characterId").GetString()!;
        }
        string character = await Character("A"), sibling = await Character("B");
        object Selection(string id, string target, string difficulty = "Normal") => new { characterId = target, dungeonId = id, difficulty };
        await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection(FirstSliceProgression.Intro1, character), null, HttpStatusCode.Unauthorized);
        JsonElement locked = await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection(FirstSliceProgression.Intro1, character), token, HttpStatusCode.Conflict);
        Require(locked.GetProperty("error").GetString() == "progression_prerequisite_not_met", "Intro1 bypassed the training prerequisite.");
        foreach (var pair in new[] { (FirstSliceProgression.Intro1, "Hard"), ("Progression.Intro3", "Normal"), ("Dungeon.RaidSwordMan", "Normal") })
            await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection(pair.Item1, character, pair.Item2), token, HttpStatusCode.BadRequest);
        Console.WriteLine("PASS Intro2 profile HTTP: fresh character stays locked; anonymous/unsupported selections rejected");

        async Task<(string Id, string Server, string Token)> Allocate(string dungeon, string expectedMap)
        {
            JsonElement session = await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection(dungeon, character), token, HttpStatusCode.Created);
            string id = session.GetProperty("dungeonSessionId").GetString()!;
            var claim = new { serverId = "intro-profile-" + id, serverAddress = "127.0.0.1:19981" };
            await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", claim, token, HttpStatusCode.Forbidden);
            await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", claim, host.AdminToken, HttpStatusCode.NoContent);
            await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/departure", new
            {
                characterId = character, requestId = Guid.NewGuid(), expectedLobbyRevision = session.GetProperty("lobbyRevision").GetInt64()
            }, token, HttpStatusCode.OK);
            JsonElement assigned = await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", claim, host.AdminToken, HttpStatusCode.OK);
            Require(assigned.GetProperty("dungeonSessionId").GetString() == id && assigned.GetProperty("dungeonId").GetString() == dungeon
                && assigned.GetProperty("serverMapPackage").GetString() == expectedMap && assigned.GetProperty("difficulty").GetString() == "Normal",
                "Allocator changed the exact content/map/difficulty.");
            return (id, claim.serverId, assigned.GetProperty("gameServerAccessToken").GetString()!);
        }
        object Reward(string server, bool ore) => new
        {
            serverId = server, rewardVersion = FirstSliceProgression.RewardVersion, changes = Array.Empty<object>(),
            itemRewards = ore ? new object[] { new
            {
                definitionType = "RPGItemDefinition", definitionName = FirstSliceEquipmentDefinitions.OreDefinitionName,
                definitionVersion = 1, quantity = 2, bindState = "CharacterBound", durabilityCurrent = 0, durabilityMaximum = 0,
                instanceTags = Array.Empty<string>(), statValues = Array.Empty<object>(), maxStackSize = FirstSliceEquipmentDefinitions.OreMaximumStackSize
            } } : Array.Empty<object>()
        };
        async Task<JsonElement> WaitGrowth(int experience)
        {
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                JsonElement state = await Send(HttpMethod.Get, $"/api/characters/{character}/progression", null, token, HttpStatusCode.OK);
                if (state.GetProperty("experience").GetInt32() == experience) return state;
                await Task.Delay(50);
            }
            throw new TestFailure("Intro1 profile settlement/progression timed out.");
        }
        var training = await Allocate(FirstSliceProgression.Training, "/Game/Maps/Progression/L_ProgressionTraining");
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{training.Id}/start", new { serverId = training.Server }, training.Token, HttpStatusCode.OK);
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{training.Id}/settle-rewards", Reward(training.Server, false), training.Token, HttpStatusCode.Accepted);
        JsonElement trained = await WaitGrowth(100);
        Require(trained.GetProperty("milestones").GetInt32() == 1 && trained.GetProperty("identityUnlocked").GetBoolean(), "Training receipt did not unlock the same character.");
        JsonElement isolated = await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection(FirstSliceProgression.Intro1, sibling), token, HttpStatusCode.Conflict);
        Require(isolated.GetProperty("error").GetString() == "progression_prerequisite_not_met", "Training unlock leaked to a sibling character.");
        Console.WriteLine("PASS Intro2 profile HTTP: actual Training settlement unlocks Intro1 for its character only");

        JsonElement secondLocked = await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection(FirstSliceProgression.Intro2, character), token, HttpStatusCode.Conflict);
        Require(secondLocked.GetProperty("error").GetString() == "progression_prerequisite_not_met", "Intro2 bypassed Intro1.");
        var intro = await Allocate(FirstSliceProgression.Intro1, "/Game/Maps/Progression/L_ProgressionIntro1");
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{intro.Id}/start", new { serverId = intro.Server }, training.Token, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{intro.Id}/start", new { serverId = intro.Server }, intro.Token, HttpStatusCode.OK);
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{intro.Id}/settle-rewards", Reward(intro.Server, true), token, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{intro.Id}/settle-rewards", Reward(intro.Server, true), intro.Token, HttpStatusCode.Accepted);
        JsonElement completed = await WaitGrowth(200);
        Require(completed.GetProperty("milestones").GetInt32() == 3 && completed.GetProperty("level").GetInt32() == 3
            && completed.GetProperty("skills").EnumerateArray().Any(skill => skill.GetProperty("skillTag").GetString() == FirstSliceProgression.Jump)
            && !completed.GetProperty("skillSlots").EnumerateArray().Any(slot => slot.GetString() == FirstSliceProgression.Jump),
            "Intro1 completion lost progression or automatically slotted the acquired skill.");
        var second = await Allocate(FirstSliceProgression.Intro2, "/Game/Maps/Progression/L_ProgressionIntro2");
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{second.Id}/start", new { serverId = second.Server }, intro.Token, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{second.Id}/start", new { serverId = second.Server }, second.Token, HttpStatusCode.OK);
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{second.Id}/settle-rewards", Reward(second.Server, true), second.Token, HttpStatusCode.Accepted);
        JsonElement secondGrowth = await WaitGrowth(300);
        Require(secondGrowth.GetProperty("milestones").GetInt32() == 7 && secondGrowth.GetProperty("totalSP").GetInt32() == 9, "Intro2 lost its existing reward/milestone policy.");
        JsonElement reread = await WaitGrowth(300);
        Require(reread.GetProperty("revision").GetString() == secondGrowth.GetProperty("revision").GetString(), "Repeated read credited Intro2 twice.");
        JsonElement snapshot = await Send(HttpMethod.Get, $"/api/dungeon-sessions/{second.Id}/reward-snapshot/{character}", null, second.Token, HttpStatusCode.OK);
        Require(snapshot.GetProperty("items").EnumerateArray().Where(item => item.GetProperty("definitionName").GetString() == FirstSliceEquipmentDefinitions.OreDefinitionName)
            .Sum(item => item.GetProperty("state").GetProperty("quantity").GetInt32()) == 4, "Cumulative Intro1+Intro2 ore2+2 reward changed.");
        Console.WriteLine("PASS Intro2 HTTP: exact three maps, server scope, real sequential receipts XP300/SP9/ore2, no duplicate growth");
    }

    private static void Require(bool value, string message) { if (!value) throw new TestFailure(message); }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (Exception error) when (error is InvalidOperationException or JsonException or FileNotFoundException) { return; }
        throw new TestFailure("Invalid Intro2 profile was accepted.");
    }
}
