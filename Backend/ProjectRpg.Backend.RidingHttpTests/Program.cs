using System.Diagnostics;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Riding;

static class Program
{
    static int assertions, sequence;
    static readonly List<object> cases = [];
    static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    static RidingOptions Options()
    {
        var o = new RidingOptions { Enabled = true, CatalogVersion = 7,
            Mounts = [new() { MountId = "horse", DefinitionVersion = 1 }],
            RegistrationItems = [new() { DefinitionType = "RPGItemDefinition", DefinitionName = "GameItem.Consume.Mount.Horse", DefinitionVersion = 1, MountId = "horse", MaximumStackSize = 99 }] };
        o.GameplayHash = new RidingCatalog(1, 7, o.Policy, [new("horse", 1, true, [])], [new(new("RPGItemDefinition", "GameItem.Consume.Mount.Horse", 1), "horse", 99, RidingItemUse.RegisterMount)]).GameplayHash;
        return o;
    }
    static void Check(bool value, string reason) { assertions++; if (!value) throw new InvalidOperationException(reason); }
    static async Task Case(string name, Func<Task> action) { int before = assertions; await action(); cases.Add(new { Name = name, Assertions = assertions - before, Passed = true }); }
    sealed record Person(string Account, GameCharacter Character, string Token, ItemRecord Item)
    {
        public string Root => "/api/characters/" + Character.CharacterId.ToString("D") + "/riding";
        public object Command(Guid request) => new { requestId = request, itemId = Item.ItemId, expectedItemRevision = "1", catalogVersion = 7 };
    }
    sealed class Chunked(string text) : HttpContent
    {
        protected override bool TryComputeLength(out long length) { length = 0; return false; }
        protected override Task SerializeToStreamAsync(Stream s, System.Net.TransportContext? c) => s.WriteAsync(Encoding.UTF8.GetBytes(text)).AsTask();
    }
    sealed class Fixture : IAsyncDisposable
    {
        public IGameRepository Game = null!; public IItemRepository Items = null!; public HttpClient Client = null!;
        public NpgsqlDataSource? Data; public readonly string Admin = Guid.NewGuid().ToString("N");
        WebApplication app = null!; bool ownsData;
        public static async Task<Fixture> Create(bool pg, RidingOptions? options = null, Fixture? shared = null)
        {
            var f = new Fixture(); var b = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing", ContentRootPath = AppContext.BaseDirectory });
            b.Logging.ClearProviders(); b.Configuration["Kestrel:Endpoints:Http:Url"] = "http://127.0.0.1:0"; b.WebHost.UseUrls("http://127.0.0.1:0");
            if (shared is not null) { f.Game = shared.Game; f.Items = shared.Items; f.Data = shared.Data; }
            else if (pg)
            {
                var cs = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_TEST_POSTGRES"));
                if (cs.Host != "127.0.0.1" || cs.Database != "riding_test") throw new InvalidOperationException("Private database required");
                f.Data = NpgsqlDataSource.Create(cs.ConnectionString); f.ownsData = true;
                var items = new PostgresItemRepository(f.Data, NullLogger<PostgresItemRepository>.Instance); f.Items = items;
                f.Game = new PostgresGameRepository(f.Data, NullLogger<PostgresGameRepository>.Instance, equipmentItems: items);
            }
            else { var gate = new InMemoryTransactionGate(); var items = new InMemoryItemRepository(gate); f.Items = items; f.Game = new InMemoryGameRepository(gate, items); }
            if (shared is null) await f.Game.EnsureCreatedAsync(default);
            b.Services.AddSingleton(f.Game); b.Services.AddSingleton<TimeProvider>(TimeProvider.System); b.Services.AddSingleton<AccessTokenService>();
            b.Services.AddSingleton<Microsoft.Extensions.Options.IOptions<AuthOptions>>(Microsoft.Extensions.Options.Options.Create(new AuthOptions { AdminToken = f.Admin })); b.Services.Configure<SecurityTelemetryOptions>(_ => { });
            b.Services.AddScoped<BearerAuthenticator>(); b.Services.AddRidingServices(options ?? Options());
            f.app = b.Build(); f.app.UseMiddleware<ApiAuthenticationMiddleware>(); f.app.MapRidingApi();
            await f.app.StartAsync(); f.Client = new() { BaseAddress = new(f.app.Urls.Single()) }; return f;
        }
        public async Task<Person> New(string? account = null)
        {
            int n = Interlocked.Increment(ref sequence); account ??= (76561700000000000L + n).ToString(); var now = DateTimeOffset.UtcNow;
            await Game.UpsertAccountAsync(new(account, account, false, false), now, default);
            var character = await Game.CreateCharacterAsync(account, "RidingHttp" + n, now, default);
            var tokens = new AccessTokenService(TimeProvider.System); var token = tokens.Issue(TimeSpan.FromHours(1)); await Game.StoreSessionAsync(token.TokenHash, account, token.ExpiresAt, default);
            var owner = new ItemOwnerRef(ItemOwnerType.Character, character.CharacterId.ToString("D"));
            var item = new ItemRecord("RPGItemDefinition", "GameItem.Consume.Mount.Horse", 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, 80),
                new(Guid.NewGuid(), 0, 2, [], []), 0, ItemLifecycleState.Active, new(ItemBindState.CharacterBound, new(0, 0), null, "Test", false));
            var seeded = await Items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "seed", owner, 2, [new(0, item)]), now, default);
            Check(seeded.Status == ItemRepositoryCommitStatus.Committed, "Real item fixture"); return new(account, character, token.Token, seeded.Records.Single());
        }
        public Task<JsonNode> Get(Person p, string? route = null, int status = 200) => Raw(p.Token, route ?? p.Root, null, status);
        public Task<JsonNode> Post(Person p, object command, int status = 200) => Raw(p.Token, p.Root + "/register", JsonSerializer.Serialize(command, Json), status);
        public async Task<JsonNode> Raw(string? token, string route, string? body, int status, bool chunked = false, string type = "application/json")
        {
            using var request = new HttpRequestMessage(body is null ? HttpMethod.Get : HttpMethod.Post, route);
            if (token is not null) request.Headers.Authorization = new("Bearer", token);
            if (body is not null) { request.Content = chunked ? new Chunked(body) : new StringContent(body, Encoding.UTF8); request.Content.Headers.ContentType = new(type); }
            using var response = await Client.SendAsync(request); string content = await response.Content.ReadAsStringAsync();
            Check((int)response.StatusCode == status, $"HTTP expected {status}, actual {(int)response.StatusCode}: {content}");
            if (status != 401) Check(response.Headers.CacheControl?.NoStore == true, "Private response no-store");
            if (status == 429) Check(response.Headers.RetryAfter is not null, "429 retry metadata");
            return string.IsNullOrEmpty(content) ? new JsonObject() : JsonNode.Parse(content)!;
        }
        public async ValueTask DisposeAsync() { Client.Dispose(); await app.StopAsync(); await app.DisposeAsync(); if (ownsData) await Data!.DisposeAsync(); }
    }
    static async Task MainCases(bool pg)
    {
        await using var f = await Fixture.Create(pg);
        await Case("http_player_auth_and_shared_store", async () =>
        {
            var p = await f.New(); var id = Guid.NewGuid();
            await f.Raw(null, p.Root, null, 401); await f.Raw("bad", p.Root, null, 401);
            await f.Raw(f.Admin, p.Root, null, 403); await f.Raw(f.Admin, p.Root + "/register", "{}", 403);
            var before = await f.Get(p); Check(before["catalogReady"]!.GetValue<bool>() && before["rosterRevision"]!.GetValue<string>() == "0", "Validated server catalog and string revision");
            var result = await f.Post(p, p.Command(id)); Check(result["outcome"]!.GetValue<string>() == "Registered", "Registration success");
            Check(result["receipt"]!["itemReceipt"]!["records"]![0]!["revision"]!.GetValue<string>() == "2", "Child revision is a JSON decimal string");
            Check(result["receipt"]!["itemReceipt"]!["records"]![0]!["metadata"]!["bindState"]!.GetValue<string>() == "CharacterBound", "Item metadata keeps existing textual enum contract");
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.State.Quantity == 1, "DI uses original common item store");
            var replay = await f.Post(p, p.Command(id)); Check(replay["outcome"]!.GetValue<string>() == "AlreadyApplied" && JsonNode.DeepEquals(result["receipt"], replay["receipt"]), "Exact receipt under replay envelope");
            var receipt = await f.Get(p, p.Root + "/requests/" + id); Check(JsonNode.DeepEquals(receipt, result["receipt"]), "Original receipt HTTP query");
            if (pg) await File.WriteAllTextAsync(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_HTTP_PROOF")!, JsonSerializer.Serialize(new Probe(p.Token, p.Root, JsonSerializer.Serialize(p.Command(id), Json), receipt)));
            var collection = await f.Get(p); Check(collection["mounts"]!.AsArray().Count == 1 && collection["rosterRevision"]!.GetValue<string>() == "1" && !collection["preferencesReady"]!.GetValue<bool>(), "Actual collection, no fake settings readiness");
            var noop = await f.Post(p, new { requestId = Guid.NewGuid(), itemId = p.Item.ItemId, expectedItemRevision = "2", catalogVersion = 7 });
            Check(noop["outcome"]!.GetValue<string>() == "AlreadyOwned" && noop["receipt"]!["itemReceipt"] is null, "AlreadyOwned does not consume");
        });
        await Case("http_owner_receipt_and_item_isolation", async () =>
        {
            var a = await f.New(); var b = await f.New(); var alt = await f.New(a.Account); var id = Guid.NewGuid(); await f.Post(a, a.Command(id));
            await f.Get(b, a.Root, 403); await f.Raw(b.Token, a.Root + "/register", JsonSerializer.Serialize(a.Command(id), Json), 403);
            await f.Get(alt, alt.Root + "/requests/" + id, 404);
            await f.Get(b, b.Root + "/requests/" + Guid.NewGuid(), 404);
            var hidden = await f.Post(b, new { requestId = Guid.NewGuid(), itemId = a.Item.ItemId, expectedItemRevision = "1", catalogVersion = 7 }, 404);
            var missing = await f.Post(b, new { requestId = Guid.NewGuid(), itemId = Guid.NewGuid(), expectedItemRevision = "1", catalogVersion = 7 }, 404);
            Check(JsonNode.DeepEquals(hidden, missing), "Foreign and missing item cannot be distinguished");
            var snapshot = await f.Get(alt); Check(snapshot["mounts"]!.AsArray().Count == 1, "Alternate character shares only roster unlock");
            var conflict = await f.Post(a, new { requestId = id, itemId = a.Item.ItemId, expectedItemRevision = "2", catalogVersion = 7 }, 409);
            Check(conflict["error"]!.GetValue<string>() == "IdempotencyConflict", "Different body cannot rewrite receipt");
        });
        await Case("http_strict_json_and_bounded_body", async () =>
        {
            var p = await f.New(); string command = JsonSerializer.Serialize(p.Command(Guid.NewGuid()), Json);
            foreach (string invalid in new[] { "null", "[]", "{}", command[..^1]+",\"quantity\":1}", command[..^1]+",\"requestId\":\""+Guid.NewGuid()+"\"}", command.Replace("\"1\"", "1"), command.Replace("\"1\"", "\"01\"") })
                await f.Raw(p.Token, p.Root + "/register", invalid, 400);
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.Revision == 1, "First malformed batch has no effects");
            p = await f.New(); command = JsonSerializer.Serialize(p.Command(Guid.NewGuid()), Json);
            foreach (string invalid in new[] { command.Replace("\"1\"", "\"9223372036854775807\""), command.Replace("\"1\"", "\"-1\""), command.Replace("catalogVersion", "CatalogVersion"), "{\"requestId\":" })
                await f.Raw(p.Token, p.Root + "/register", invalid, 400);
            await f.Raw(p.Token, p.Root + "/register", command, 415, type: "text/plain");
            await f.Raw(p.Token, p.Root + "/register", command + new string(' ', 16384), 413);
            await f.Raw(p.Token, p.Root + "/register", command + new string(' ', 16384), 413, chunked: true);
            await f.Get(p, p.Root + "?world=other", 400);
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.Revision == 1, "Malformed requests have no item effects");
        });
        await Case("http_disabled_write_with_receipt_recovery", async () =>
        {
            var p = await f.New(); var id = Guid.NewGuid(); var original = await f.Post(p, p.Command(id));
            await using var off = await Fixture.Create(pg, new RidingOptions(), f);
            var replay = await off.Post(p, p.Command(id)); Check(replay["outcome"]!.GetValue<string>() == "AlreadyApplied" && JsonNode.DeepEquals(original["receipt"], replay["receipt"]), "Disable does not strand uncertain success");
            await off.Get(p, p.Root + "/requests/" + id);
            var current = await off.Get(p); Check(!current["catalogReady"]!.GetValue<bool>() && current["mounts"]!.AsArray().Count == 1, "Read collection remains available but not ready");
            var denied = await off.Post(p, new { requestId = Guid.NewGuid(), itemId = p.Item.ItemId, expectedItemRevision = "2", catalogVersion = 7 }, 404);
            Check(denied["error"]!.GetValue<string>() == "RidingDisabled" && (await f.Items.FindAsync(p.Item.ItemId, default))!.State.Quantity == 1, "Disabled new request has no write");
        });
        await Case("http_lobby_cas_and_server_credentials", async () =>
        {
            var p = await f.New(); await f.Post(p, new { requestId = Guid.NewGuid(), itemId = p.Item.ItemId, expectedItemRevision = "2", catalogVersion = 7 }, 409);
            await f.Post(p, new { requestId = Guid.NewGuid(), itemId = p.Item.ItemId, expectedItemRevision = "1", catalogVersion = 8 }, 400);
            var now = DateTimeOffset.UtcNow; var session = await f.Game.CreateDungeonSessionAsync(p.Account, p.Character.CharacterId, "fixture", "Normal", now, now.AddHours(1), default);
            await f.Post(p, p.Command(Guid.NewGuid()), 409);
            var tokens = new AccessTokenService(TimeProvider.System); var server = tokens.Issue(TimeSpan.FromHours(1));
            await f.Game.AssignDungeonServerAsync(session.DungeonSessionId, "riding-server", "127.0.0.1:1", now, now.AddHours(1), default);
            await f.Game.StoreGameServerCredentialAsync(server.TokenHash, "riding-server", session.DungeonSessionId, server.ExpiresAt, default);
            await f.Raw(server.Token, p.Root, null, 403); await f.Raw(server.Token, p.Root + "/register", "{}", 403);
        });
        await Case("http_account_budgets_and_retry", async () =>
        {
            var p = await f.New(); var other = await f.New(); int readDenied = 0;
            for (int i = 0; i < 20; i++)
            {
                using var req = new HttpRequestMessage(HttpMethod.Get, p.Root); req.Headers.Authorization = new("Bearer", p.Token);
                using var r = await f.Client.SendAsync(req); if ((int)r.StatusCode == 429) { readDenied++; Check(r.Headers.RetryAfter is not null && r.Headers.CacheControl?.NoStore == true, "429 headers"); } else Check(r.IsSuccessStatusCode, "Read budget response");
            }
            Check(readDenied > 0, "Read burst bounded"); await f.Get(other);
            int writesDenied = 0;
            for (int i = 0; i < 20; i++)
            {
                using var req = new HttpRequestMessage(HttpMethod.Post, p.Root + "/register") { Content = new StringContent("{}", Encoding.UTF8, "application/json") }; req.Headers.Authorization = new("Bearer", p.Token);
                using var r = await f.Client.SendAsync(req); if ((int)r.StatusCode == 429) writesDenied++; else Check((int)r.StatusCode == 400, "Separate write budget");
            }
            Check(writesDenied > 0, "Write burst bounded"); await Task.Delay(1100); await f.Get(p); await f.Raw(p.Token, p.Root + "/register", "{}", 400);
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.Revision == 1, "Rate rejection has no persistence effects");
        });
        await Case("http_config_validation_and_int64_projection", async () =>
        {
            foreach (var o in new[] { new RidingOptions { Enabled = true }, Options() })
            {
                o.GameplayHash = "bad"; bool rejected = false; try { new ServiceCollection().AddRidingServices(o); } catch (Exception e) when (e is RidingRuleException or InvalidOperationException) { rejected = true; }
                Check(rejected, "Invalid enabled catalog fails startup");
            }
            var opts = Options(); var services = new ServiceCollection(); services.AddRidingServices(opts); opts.Enabled = false; opts.Mounts[0].MountId = "changed";
            using var provider = services.BuildServiceProvider(); Check(provider.GetRequiredService<RidingRuntime>().Catalog!.Mount("horse").MountId == "horse", "Authoring options cannot mutate frozen startup catalog");
            const long large = 9007199254740993;
            var receipt = new RidingRegistrationReceipt(Guid.NewGuid(), "Riding.Register", "internal", Guid.NewGuid(), Guid.NewGuid(), RidingRegistrationOutcome.AlreadyOwned, "horse", large, DateTimeOffset.UtcNow, null);
            var json = JsonNode.Parse(JsonSerializer.Serialize(RidingApiContracts.Receipt(receipt), Json))!;
            Check(json["rosterRevision"]!.GetValue<string>() == "9007199254740993" && json["fingerprint"] is null, "Large int64 is lossless and internal fingerprint omitted");
            await Task.CompletedTask;
        });
    }
    sealed record Probe(string Token, string Root, string Command, JsonNode Receipt);
    static async Task ProgramHost(bool enabled)
    {
        var probe = JsonSerializer.Deserialize<Probe>(await File.ReadAllTextAsync(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_HTTP_PROOF")!))!;
        var start = new ProcessStartInfo("dotnet") { WorkingDirectory = AppContext.BaseDirectory, UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        start.ArgumentList.Add(typeof(RidingOptions).Assembly.Location);
        var connection = Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_TEST_POSTGRES")!;
        foreach (var (name, value) in new[] { ("DOTNET_ENVIRONMENT", "Testing"), ("ASPNETCORE_ENVIRONMENT", "Testing"),
            ("Storage__Provider", "Postgres"), ("ConnectionStrings__Postgres", connection), ("POSTGRES_CONNECTION_STRING", connection),
            ("Auth__AdminToken", Guid.NewGuid().ToString("N")), ("Steam__AllowDevelopmentAuthentication", "true"), ("DungeonContent__DevelopmentProfile", "Training"),
            ("Kestrel__Endpoints__Http__Url", "http://127.0.0.1:0"), ("Auctions__Enabled", "false"), ("RateLimits__Enabled", "false"),
            ("Logging__LogLevel__Default", "Warning"), ("Logging__LogLevel__Microsoft.Hosting.Lifetime", "Information"),
            ("Riding__Enabled", enabled ? "true" : "false"), ("Riding__CatalogVersion", "7"), ("Riding__GameplayHash", Options().GameplayHash!),
            ("Riding__Mounts__0__MountId", "horse"), ("Riding__Mounts__0__DefinitionVersion", "1"),
            ("Riding__RegistrationItems__0__DefinitionType", "RPGItemDefinition"), ("Riding__RegistrationItems__0__DefinitionName", "GameItem.Consume.Mount.Horse"),
            ("Riding__RegistrationItems__0__DefinitionVersion", "1"), ("Riding__RegistrationItems__0__MountId", "horse"), ("Riding__RegistrationItems__0__MaximumStackSize", "99") }) start.Environment[name] = value;
        start.Environment.Remove("PROJECT_RPG_BACKEND_ADMIN_TOKEN"); start.Environment.Remove("STEAM_WEB_API_PUBLISHER_KEY");
        var listening = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
        var tail = new System.Collections.Concurrent.ConcurrentQueue<string>();
        void Observe(object sender, DataReceivedEventArgs e)
        {
            if (e.Data is not { } text) return; tail.Enqueue(text); while (tail.Count > 8) tail.TryDequeue(out _);
            const string marker = "Now listening on: "; int at = text.IndexOf(marker, StringComparison.Ordinal); if (at >= 0) listening.TrySetResult(text[(at + marker.Length)..]);
        }
        using var child = new Process { StartInfo = start, EnableRaisingEvents = true };
        child.OutputDataReceived += Observe; child.ErrorDataReceived += Observe;
        child.Exited += (_, _) => listening.TrySetException(new InvalidOperationException("Program failed before listening: " + string.Join(" / ", tail)));
        Check(child.Start(), "Own production-composition process starts"); child.BeginOutputReadLine(); child.BeginErrorReadLine();
        try
        {
            using var client = new HttpClient { BaseAddress = new(await listening.Task.WaitAsync(TimeSpan.FromSeconds(30))) };
            client.DefaultRequestHeaders.Authorization = new("Bearer", probe.Token);
            using var get = await client.GetAsync(probe.Root); Check(get.IsSuccessStatusCode, "Actual Program authenticates existing player");
            var collection = JsonNode.Parse(await get.Content.ReadAsStringAsync())!;
            Check(collection["catalogReady"]!.GetValue<bool>() == enabled, "Actual startup binds/validates configured catalog and disabled mode");
            using var receipt = await client.GetAsync(probe.Root + "/requests/" + JsonNode.Parse(probe.Command)!["requestId"]!.GetValue<string>());
            Check(receipt.IsSuccessStatusCode && JsonNode.DeepEquals(JsonNode.Parse(await receipt.Content.ReadAsStringAsync()), probe.Receipt), "Fresh Program recovers persisted original HTTP receipt");
            using var replay = await client.PostAsync(probe.Root + "/register", new StringContent(probe.Command, Encoding.UTF8, "application/json"));
            var result = JsonNode.Parse(await replay.Content.ReadAsStringAsync())!;
            Check(replay.IsSuccessStatusCode && result["outcome"]!.GetValue<string>() == "AlreadyApplied" && JsonNode.DeepEquals(result["receipt"], probe.Receipt), "Actual Program replay uses original shared PG store");
        }
        finally { if (!child.HasExited) { child.Kill(true); await child.WaitForExitAsync(); } }
    }
    static async Task<int> Main(string[] args)
    {
        try { bool pg = args.SequenceEqual(new[] { "--postgres" }); bool program = args.SequenceEqual(new[] { "--program" }); if (args.Length > 0 && !pg && !program) throw new ArgumentException("Unknown mode");
            if (program) { await Case("http_actual_program_enabled", () => ProgramHost(true)); await Case("http_actual_program_disabled", () => ProgramHost(false)); } else await MainCases(pg);
            Console.WriteLine(JsonSerializer.Serialize(new { Passed = true, Mode = program ? "Program" : pg ? "Postgres" : "Memory", CaseCount = cases.Count, Assertions = assertions, Cases = cases })); return 0; }
        catch (Exception e) { Console.Error.WriteLine(e); Console.WriteLine(JsonSerializer.Serialize(new { Passed = false, Assertions = assertions, Cases = cases })); return 1; }
    }
}
