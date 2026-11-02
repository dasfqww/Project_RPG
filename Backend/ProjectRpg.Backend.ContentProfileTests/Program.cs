using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Hosting;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;

try
{
    if (args is ["--intro1-only"])
    {
        await Intro1DevelopmentProfileTests.Run();
        return 0;
    }
    if (args is ["--personal-loot-http-only"])
    {
        await DungeonPersonalLootTests.Http();
        return 0;
    }
    if (args is ["--personal-loot-postgres-only"])
    {
        await DungeonPersonalLootTests.Postgres(required: true);
        return 0;
    }
    if (args is ["--personal-loot-only"])
    {
        await DungeonPersonalLootTests.Run();
        return 0;
    }
    if (args is ["--legacy-inventory-storage-only"])
    {
        await LegacyInventoryScopeTests.Storage();
        return 0;
    }
    if (args is ["--legacy-inventory-scope-only"])
    {
        await LegacyInventoryScopeTests.Run();
        return 0;
    }
    if (args is ["--server-endpoint-only"])
    {
        await DungeonServerEndpointTests.Run();
        return 0;
    }
    if (args is ["--session-read-scope-only"])
    {
        await DungeonSessionQueryScopeTests.Run();
        return 0;
    }
    Require(args.Length == 0, "Unknown content profile test arguments.");
    RunConfigurationChecks();
    await RunHttpChecksAsync();
    await Intro1DevelopmentProfileTests.Run();
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine("FAIL Profile: " + (exception is TestFailure ? exception.Message : exception.GetType().Name));
    return 1;
}

static ConfigurationManager Configuration(string? profile = null)
{
    ConfigurationManager configuration = new();
    configuration.AddInMemoryCollection(new Dictionary<string, string?>
    {
        ["DungeonContent:Entries:0:DungeonId"] = "Dungeon.RaidSwordMan",
        ["DungeonContent:Entries:0:MapPackage"] = "/Game/Maps/bossmap",
        ["DungeonContent:Entries:0:Difficulties:0"] = "Easy",
        ["DungeonContent:Entries:0:Difficulties:1"] = "Normal",
        ["DungeonContent:Entries:0:Difficulties:2"] = "Hard",
        ["DungeonContent:Entries:0:Difficulties:3"] = "Hell",
        ["DungeonSessions:MaxPartySize"] = "4",
        ["DungeonSessions:MinimumDeparturePartySize"] = "2",
        ["Steam:AllowDevelopmentAuthentication"] = "false",
        ["Storage:Provider"] = "Postgres",
        ["Auth:AdminToken"] = "sentinel-not-a-real-secret"
    });
    if (profile is not null) configuration[DungeonDevelopmentProfiles.Setting] = profile;
    return configuration;
}

static void RunConfigurationChecks()
{
    foreach (string environment in new[] { "Development", "Testing", "Production", "Staging" })
    {
        ConfigurationManager config = Configuration();
        DungeonContentCatalog original = DungeonDevelopmentProfiles.Resolve(config, new TestEnvironment(environment), AppContext.BaseDirectory);
        Require(original.AllowedSelections.Count == 4 && original.GetSelectionError("Dungeon.RaidSwordMan", "Hard") is null
            && original.GetSelectionError("Progression.Training", "Normal") is not null
            && config["DungeonSessions:MaxPartySize"] == "4", "Default behavior changed without opt-in.");
    }
    Console.WriteLine("PASS Profile: omitted profile preserves existing catalogs and party settings in all environments");
    foreach (string environment in new[] { "Development", "Testing" })
    {
        ConfigurationManager config = Configuration("Training");
        config["DungeonContent:Entries:7:DungeonId"] = "MustNotMerge";
        DungeonContentCatalog selected = DungeonDevelopmentProfiles.Resolve(config, new TestEnvironment(environment), AppContext.BaseDirectory);
        Require(selected.AllowedSelections.Count == 1 && selected.GetSelectionError("Progression.Training", "Normal") is null
            && selected.GetMapPackage("Progression.Training", "Normal") == "/Game/Maps/Progression/L_ProgressionTraining"
            && selected.GetSelectionError("Progression.Training", "Hard") is not null
            && selected.GetSelectionError("Dungeon.RaidSwordMan", "Normal") is not null,
            "Default or injected catalog entries/difficulties leaked into the profile.");
        Require(config["DungeonSessions:MaxPartySize"] == "1" && config["DungeonSessions:MinimumDeparturePartySize"] == "1"
            && config["Steam:AllowDevelopmentAuthentication"] == "false" && config["Storage:Provider"] == "Postgres"
            && config["Auth:AdminToken"] == "sentinel-not-a-real-secret", "Profile changed unrelated security/storage configuration.");
    }
    Console.WriteLine("PASS Profile: complete Training/Normal snapshot and solo limit without auth/storage overrides");
    foreach (string name in new[] { "", " ", "training", "Training ", "../Training", "Training.json" })
    {
        ConfigurationManager config = Configuration(name);
        Reject(() => DungeonDevelopmentProfiles.Resolve(config, new TestEnvironment("Development"), "not-a-directory"));
        Require(config["DungeonSessions:MaxPartySize"] == "4", "Rejected name changed party configuration.");
    }
    foreach (string environment in new[] { "Production", "Staging" })
        Reject(() => DungeonDevelopmentProfiles.Resolve(Configuration("Training"), new TestEnvironment(environment), "not-a-directory"));
    Console.WriteLine("PASS Profile: unknown/path-like names and non-development environments fail closed");

    string fixture = Path.Combine(AppContext.BaseDirectory, "ProfileFixture_" + Guid.NewGuid().ToString("N"));
    string file = Path.Combine(fixture, "DevelopmentProfiles", "Training.json");
    string packaged = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "DevelopmentProfiles", "Training.json"));
    Directory.CreateDirectory(Path.GetDirectoryName(file)!);
    try
    {
        foreach (string invalid in new[] { "null", "{}", packaged.Replace("\"schemaVersion\": 1", "\"schemaVersion\": 2"),
            packaged.Replace("\"maximumPartySize\": 1", "\"maximumPartySize\": 5"),
            packaged.Replace("\"minimumPartySize\": 1", "\"minimumPartySize\": 2"),
            packaged.Replace("/Game/Maps/Progression/L_ProgressionTraining", "/Game/Maps/A?game=Unsafe"),
            packaged.Replace("\"Normal\"", "\"Normal\", \"Normal\""),
            packaged.Replace("\"schemaVersion\": 1", "\"unknown\": true, \"schemaVersion\": 1") })
        {
            File.WriteAllText(file, invalid);
            ConfigurationManager config = Configuration("Training");
            Reject(() => DungeonDevelopmentProfiles.Resolve(config, new TestEnvironment("Development"), fixture));
            Require(config["DungeonSessions:MaxPartySize"] == "4", "Invalid document partly applied settings.");
        }
        File.WriteAllText(file, packaged);
        DungeonContentCatalog snapshot = DungeonDevelopmentProfiles.Resolve(Configuration("Training"), new TestEnvironment("Development"), fixture);
        File.WriteAllText(file, "{}");
        Require(snapshot.GetMapPackage("Progression.Training", "Normal") == "/Game/Maps/Progression/L_ProgressionTraining",
            "A file rewrite mutated a running catalog snapshot.");
    }
    finally
    {
        // Only the exact generated fixture under this disposable build output.
        Require(Path.GetDirectoryName(fixture) == AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar)
            && !File.GetAttributes(fixture).HasFlag(FileAttributes.ReparsePoint), "Unsafe profile fixture cleanup.");
        Directory.Delete(fixture, true);
    }
    Console.WriteLine("PASS Profile: malformed/unsafe metadata is atomic and loaded catalogs do not hot-reload");
}

static async Task RunHttpChecksAsync()
{
    using ProfileHost host = new("Development");
    Uri address = await host.StartAsync();
    using HttpClient http = new() { BaseAddress = address, Timeout = TimeSpan.FromSeconds(10) };
    async Task<JsonElement> Send(HttpMethod method, string path, object? body, string? token, HttpStatusCode expected)
    {
        using HttpRequestMessage request = new(method, path);
        if (body is not null) request.Content = JsonContent.Create(body);
        if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
        using HttpResponseMessage response = await http.SendAsync(request);
        Require(response.StatusCode == expected, $"Unexpected HTTP status for {method} {path}: {(int)response.StatusCode}.");
        if (response.Content.Headers.ContentLength == 0 || expected == HttpStatusCode.NoContent) return default;
        return await response.Content.ReadFromJsonAsync<JsonElement>();
    }
    async Task<(string Token, string Character)> Player(string steamId, string name)
    {
        JsonElement auth = await Send(HttpMethod.Post, "/api/auth/steam-ticket", new { ticket = "dev:" + steamId }, null, HttpStatusCode.OK);
        string token = auth.GetProperty("accessToken").GetString()!;
        JsonElement character = await Send(HttpMethod.Post, "/api/characters", new { name, classId = "RPGPlayerClassDefinition:GreatSwordWarrior" }, token, HttpStatusCode.Created);
        return (token, character.GetProperty("characterId").GetString()!);
    }
    var first = await Player("76561198000000191", "ProfileFirst");
    var second = await Player("76561198000000192", "ProfileSecond");
    object Selection(string id, string difficulty) => new { characterId = first.Character, dungeonId = id, difficulty };
    await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection("Progression.Training", "Normal"), null, HttpStatusCode.Unauthorized);
    foreach (var pair in new[] { ("Progression.Training", "Hard"), ("Progression.Intro1", "Normal"), ("Dungeon.RaidSwordMan", "Normal") })
        await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection(pair.Item1, pair.Item2), first.Token, HttpStatusCode.BadRequest);
    JsonElement session = await Send(HttpMethod.Post, "/api/dungeon-sessions", Selection("Progression.Training", "Normal"), first.Token, HttpStatusCode.Created);
    string id = session.GetProperty("dungeonSessionId").GetString()!;
    await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/members", new { characterId = second.Character }, second.Token, HttpStatusCode.Conflict);
    JsonElement unchanged = await Send(HttpMethod.Get, $"/api/dungeon-sessions/{id}", null, first.Token, HttpStatusCode.OK);
    Require(unchanged.GetProperty("members").GetArrayLength() == 1
        && unchanged.GetProperty("lobbyRevision").GetInt64() == session.GetProperty("lobbyRevision").GetInt64(),
        "Denied second join changed the solo lobby.");
    Console.WriteLine("PASS Profile HTTP: authenticated Normal creation, unsupported selection/anonymous/second-member rejection");
    var claim = new { serverId = "profile-test-server", serverAddress = "127.0.0.1:19997" };
    await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", claim, first.Token, HttpStatusCode.Forbidden);
    await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", claim, host.AdminToken, HttpStatusCode.NoContent);
    await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/departure", new
    {
        characterId = first.Character, requestId = Guid.NewGuid(), expectedLobbyRevision = session.GetProperty("lobbyRevision").GetInt64()
    }, first.Token, HttpStatusCode.OK);
    JsonElement assigned = await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", claim, host.AdminToken, HttpStatusCode.OK);
    Require(assigned.GetProperty("dungeonSessionId").GetString() == id
        && assigned.GetProperty("serverMapPackage").GetString() == "/Game/Maps/Progression/L_ProgressionTraining"
        && assigned.GetProperty("difficulty").GetString() == "Normal"
        && !string.IsNullOrEmpty(assigned.GetProperty("gameServerAccessToken").GetString()), "Assigned map/session/token did not match the profile.");
    Console.WriteLine("PASS Profile HTTP: leader departure gates the real admin allocator and trusted training map assignment");
    using ProfileHost production = new("Production");
    await production.ExpectRejectionAsync("Dungeon development profiles require Development or Testing.");
    Console.WriteLine("PASS Profile HTTP: actual Production startup rejects the development profile before listening");
}

static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
static void Reject(Action body)
{
    try { body(); }
    catch (Exception exception) when (exception is InvalidOperationException or JsonException) { return; }
    throw new TestFailure("Invalid profile was accepted.");
}
sealed class TestFailure(string message) : Exception(message);
sealed class TestEnvironment(string name) : IHostEnvironment
{
    public string EnvironmentName { get; set; } = name;
    public string ApplicationName { get; set; } = "ProfileTests";
    public string ContentRootPath { get; set; } = AppContext.BaseDirectory;
    public IFileProvider ContentRootFileProvider { get; set; } = new NullFileProvider();
}

sealed class ProfileHost : IDisposable
{
    public string AdminToken { get; } = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
    private readonly Process child;
    private readonly Task<string> errors;
    public ProfileHost(string environment, string profile = "Training")
    {
        ProcessStartInfo start = new("dotnet")
        {
            UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true, RedirectStandardError = true, WorkingDirectory = AppContext.BaseDirectory
        };
        start.ArgumentList.Add(typeof(DungeonDevelopmentProfiles).Assembly.Location);
        start.Environment["ASPNETCORE_ENVIRONMENT"] = environment;
        start.Environment["DOTNET_ENVIRONMENT"] = environment;
        start.Environment.Remove("POSTGRES_CONNECTION_STRING");
        start.Environment.Remove("ConnectionStrings__Postgres");
        start.Environment["Storage__Provider"] = "Memory";
        start.Environment["Steam__AllowDevelopmentAuthentication"] = "true";
        start.Environment["DungeonContent__DevelopmentProfile"] = profile;
        start.Environment["DungeonContent__Entries__7__DungeonId"] = "MustNotMerge";
        start.Environment["DungeonSessions__MaxPartySize"] = "4";
        start.Environment["PROJECT_RPG_BACKEND_ADMIN_TOKEN"] = AdminToken;
        start.Environment["Kestrel__Endpoints__Http__Url"] = "http://127.0.0.1:0";
        start.Environment["Logging__LogLevel__Default"] = "Warning";
        start.Environment["Logging__LogLevel__Microsoft.Hosting.Lifetime"] = "Information";
        child = new() { StartInfo = start };
        if (!child.Start()) throw new TestFailure("Unable to start own profile host.");
        errors = child.StandardError.ReadToEndAsync();
    }
    public async Task<Uri> StartAsync()
    {
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(25));
        while (await child.StandardOutput.ReadLineAsync(timeout.Token) is string line)
        {
            const string marker = "Now listening on: ";
            int index = line.IndexOf(marker, StringComparison.Ordinal);
            if (index < 0) continue;
            Uri address = new(line[(index + marker.Length)..].Trim());
            if (!address.IsLoopback) throw new TestFailure("Host did not bind to loopback.");
            _ = child.StandardOutput.ReadToEndAsync();
            return address;
        }
        throw new TestFailure("Own profile host did not start.");
    }
    public async Task ExpectRejectionAsync(string expected)
    {
        Task<string> output = child.StandardOutput.ReadToEndAsync();
        await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(25));
        string log = await output + await errors;
        if (child.ExitCode == 0 || !log.Contains(expected, StringComparison.Ordinal) || log.Contains("Now listening", StringComparison.Ordinal))
            throw new TestFailure("Production profile did not fail before hosting.");
    }
    public void Dispose()
    {
        try { if (!child.HasExited) { child.Kill(entireProcessTree: true); if (!child.WaitForExit(10000)) throw new TestFailure("Owned profile host still running."); } }
        finally { child.Dispose(); }
    }
}
