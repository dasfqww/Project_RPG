using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class CatalogCases
{
    private static int checks;
    private sealed record Person(string Account, Guid Character, string Token);
    private static Dictionary<string, string?> Entry(string name = "Catalog.Ore", bool direct = true, bool auction = false, int index = 0) => new()
    {
        [$"Trading:Items:{index}:DefinitionType"] = "Material",
        [$"Trading:Items:{index}:DefinitionName"] = name,
        [$"Trading:Items:{index}:DefinitionVersion"] = "1",
        [$"Trading:Items:{index}:AllowDirect"] = direct.ToString(),
        [$"Trading:Items:{index}:AllowAuction"] = auction.ToString(),
        [$"Trading:Items:{index}:MaximumStackSize"] = "100"
    };
    private static IConfigurationRoot Config(Dictionary<string, string?> values) => new ConfigurationBuilder().AddInMemoryCollection(values).Build();
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); checks++; }
    private static ItemRecord Item(Person person, string name, int quantity = 10, int version = 1, int slot = 160)
    {
        var owner = new ItemOwnerRef(ItemOwnerType.Character, person.Character.ToString("D"));
        return new("Material", name, version, owner, new(ItemContainerType.Inventory, owner.OwnerId, slot),
            new(Guid.NewGuid(), 17, quantity, [], []), 1, ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, "Catalog", false));
    }
    private static void Validate(TradeItemPolicy policy, ItemRecord item, Person person, TradeChannel channel, string? error = null)
    {
        try
        {
            policy.ValidateOfferItem(item, new(person.Account, person.Character), new(item.ItemId, item.Revision, 1), channel, DateTimeOffset.UtcNow);
            Check(error is null, "item unexpectedly allowed");
        }
        catch (TradeRuleException failure) when (failure.Code == error) { checks++; }
    }
    private static void Parsing()
    {
        var person = new Person("76561198881411001", Guid.NewGuid(), "unused"); var item = Item(person, "Catalog.Ore");
        Validate(TradeItemCatalogConfiguration.Load(Config([])), item, person, TradeChannel.Direct, "item_trade_not_allowed");
        using (var json = new MemoryStream(Encoding.UTF8.GetBytes("{\"Trading\":{\"Items\":[]}}")))
            Validate(TradeItemCatalogConfiguration.Load(new ConfigurationBuilder().AddJsonStream(json).Build()), item, person, TradeChannel.Auction, "item_trade_not_allowed");
        var values = Entry(); var root = Config(values); var policy = TradeItemCatalogConfiguration.Load(root);
        Validate(policy, item, person, TradeChannel.Direct); Validate(policy, item, person, TradeChannel.Auction, "item_trade_not_allowed");
        Validate(policy, item with { DefinitionVersion = 2 }, person, TradeChannel.Direct, "item_trade_not_allowed");
        Validate(policy, item with { DefinitionName = "catalog.ore" }, person, TradeChannel.Direct, "item_trade_not_allowed");
        Validate(policy, Item(person, "Catalog.Ore", 101), person, TradeChannel.Direct, "invalid_trade_quantity");
        root["Trading:Items:0:AllowAuction"] = "true";
        Validate(policy, item, person, TradeChannel.Auction, "item_trade_not_allowed");
        Validate(TradeItemCatalogConfiguration.Load(root), item, person, TradeChannel.Auction);
        var invalid = new List<Dictionary<string, string?>>();
        foreach (var (key, value) in new (string, string?)[] { ("DefinitionVersion", "0"), ("DefinitionVersion", "2147483648"),
            ("DefinitionVersion", "1.0"), ("MaximumStackSize", "-1"), ("AllowAuction", "yes"), ("AllowDirect", "1"),
            ("DefinitionType", " Material"), ("DefinitionName", ""), ("DefinitionName", "Ore\n"), ("DefinitionName", new string('a', 129)) })
        { var bad = Entry(); bad["Trading:Items:0:" + key] = value; invalid.Add(bad); }
        foreach (string field in new[] { "AllowDirect", "AllowAuction", "DefinitionType", "MaximumStackSize" })
        { var bad = Entry(); bad.Remove("Trading:Items:0:" + field); invalid.Add(bad); }
        var unknown = Entry(); unknown["Trading:Items:0:AllowAucton"] = "true"; invalid.Add(unknown);
        var nested = Entry(); nested["Trading:Items:0:AllowAuction:child"] = "true"; invalid.Add(nested);
        var duplicate = Entry(); foreach (var pair in Entry(index: 1)) duplicate.Add(pair.Key, pair.Value); invalid.Add(duplicate);
        invalid.Add(Entry(index: 1)); invalid.Add(Entry().ToDictionary(p => p.Key.Replace(":0:", ":00:"), p => p.Value));
        invalid.Add(new() { ["Trading:Items"] = "not-a-list" }); invalid.Add(new() { ["Trading:Items:0"] = null });
        foreach (var bad in invalid)
        {
            try { TradeItemCatalogConfiguration.Load(Config(bad)); throw new Exception("invalid catalog accepted"); }
            catch (InvalidOperationException failure) when (failure.Message.StartsWith("Trading:Items", StringComparison.Ordinal)) { checks++; }
        }
        var many = new Dictionary<string, string?>(); for (int i = 0; i < 12; i++) foreach (var p in Entry("Catalog.Ore" + i, index: i)) many.Add(p.Key, p.Value);
        var manyPolicy = TradeItemCatalogConfiguration.Load(Config(many));
        Validate(manyPolicy, Item(person, "Catalog.Ore11"), person, TradeChannel.Direct);
        var priorCulture = CultureInfo.CurrentCulture;
        try { CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("tr-TR"); Validate(TradeItemCatalogConfiguration.Load(Config(Entry())), item, person, TradeChannel.Direct); }
        finally { CultureInfo.CurrentCulture = priorCulture; }
    }
    public static async Task<int> Run()
    {
        try
        {
            Parsing();
            string connection = Environment.GetEnvironmentVariable("PROJECT_RPG_CATALOG_TEST_POSTGRES") ?? throw new Exception("Private PG fixture required");
            var cs = new NpgsqlConnectionStringBuilder(connection);
            if (cs.Host != "127.0.0.1" || cs.Database != "trade_catalog_test" || !Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_CATALOG_TEST_RUN"), "N", out _))
                throw new Exception("Private database/run required");
            await using var data = NpgsqlDataSource.Create(connection);
            var game = new PostgresGameRepository(data, NullLogger<PostgresGameRepository>.Instance);
            var items = new PostgresItemRepository(data, NullLogger<PostgresItemRepository>.Instance);
            await game.EnsureCreatedAsync(default);
            await new PostgresEconomyRepository(data).UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Account, 1000, true), DateTimeOffset.UtcNow, default);
            async Task<Person> Create(string account, string name)
            {
                var now = DateTimeOffset.UtcNow; await game.UpsertAccountAsync(new(account, account, false, false), now, default);
                var character = await game.CreateCharacterAsync(account, name, now, default);
                var token = new AccessTokenService(TimeProvider.System).Issue(TimeSpan.FromHours(1)); await game.StoreSessionAsync(token.TokenHash, account, token.ExpiresAt, default);
                return new(account, character.CharacterId, token.Token);
            }
            var seller = await Create("76561198881411001", "CatalogSeller"); var buyer = await Create("76561198881411002", "CatalogBuyer");
            async Task<ItemRecord> Seed(string name, int slot, int quantity = 10, int version = 1)
            {
                var raw = Item(seller, name, quantity, version, slot) with { Revision = 0 };
                var result = await items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "catalog", raw.Owner, quantity, [new(0, raw)]), DateTimeOffset.UtcNow, default);
                Check(result.Status == ItemRepositoryCommitStatus.Committed, "fixture seed"); return result.Records.Single();
            }
            var ore = await Seed("Catalog.Ore", 160); var crystal = await Seed("Catalog.Crystal", 161);
            var newer = await Seed("Catalog.Crystal", 162, version: 2); var oversize = await Seed("Catalog.Crystal", 163, 101);
            string Root(Person p) => $"/api/characters/{p.Character}/auctions";
            object Register(ItemRecord item, Guid request) => new { requestId = request, itemId = item.ItemId, expectedItemRevision = item.Revision.ToString(CultureInfo.InvariantCulture), quantity = 1, totalPrice = "100", expiresAt = DateTimeOffset.UtcNow.AddHours(1).ToString("O") };
            var catalog = Entry(); foreach (var pair in Entry("Catalog.Crystal", false, true, 1)) catalog.Add(pair.Key, pair.Value);
            await using (var server = await Server.Start(connection, [], enabled: true))
            {
                await server.Send(seller, Root(seller) + $"/{Guid.NewGuid()}/register", Register(crystal, Guid.NewGuid()), 409, "item_trade_not_allowed");
                Check((await items.FindAsync(crystal.ItemId, default))!.Revision == 1, "default catalog denies without moving item");
            }
            Guid listing = Guid.NewGuid(), requestId = Guid.NewGuid(); object original = Register(crystal, requestId); JsonNode? receipt;
            await using (var server = await Server.Start(connection, catalog, enabled: true))
            {
                await server.Send(seller, Root(seller) + $"/{Guid.NewGuid()}/register", Register(ore, Guid.NewGuid()), 409, "item_trade_not_allowed");
                await server.Send(seller, Root(seller) + $"/{Guid.NewGuid()}/register", Register(newer, Guid.NewGuid()), 409, "item_trade_not_allowed");
                await server.Send(seller, Root(seller) + $"/{Guid.NewGuid()}/register", Register(oversize, Guid.NewGuid()), 409, "invalid_trade_quantity");
                receipt = await server.Send(seller, Root(seller) + $"/{listing}/register", original);
                Check((await items.FindAsync(crystal.ItemId, default))!.State.Quantity == 9, "real Program configured auction policy escrows once");
                string a = $"/api/characters/{seller.Character}/trades/{Guid.NewGuid()}"; string b = a.Replace(seller.Character.ToString(), buyer.Character.ToString());
                var session = (await server.Send(seller, a + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = buyer.Character }))!["session"]!;
                session = (await server.Send(buyer, b + "/accept", new { requestId = Guid.NewGuid(), expectedRevision = session["revision"]!.GetValue<string>() }))!["session"]!;
                session = (await server.Send(seller, a + "/offer", new { requestId = Guid.NewGuid(), expectedRevision = session["revision"]!.GetValue<string>(), offer = new { items = new[] { new { itemId = ore.ItemId, expectedRevision = "1", quantity = 1 } }, currencyAmount = "0" } }))!["session"]!;
                session = (await server.Send(seller, a + "/ready", new { requestId = Guid.NewGuid(), expectedRevision = session["revision"]!.GetValue<string>(), expectedOfferRevision = session["offerRevision"]!.GetValue<string>() }))!["session"]!;
                session = (await server.Send(seller, a + "/withdraw", new { requestId = Guid.NewGuid(), expectedRevision = session["revision"]!.GetValue<string>() }))!["session"]!;
                session = (await server.Send(seller, a + "/offer", new { requestId = Guid.NewGuid(), expectedRevision = session["revision"]!.GetValue<string>(), offer = new { items = new[] { new { itemId = crystal.ItemId, expectedRevision = "2", quantity = 1 } }, currencyAmount = "0" } }))!["session"]!;
                session = (await server.Send(seller, a + "/ready", new { requestId = Guid.NewGuid(), expectedRevision = session["revision"]!.GetValue<string>(), expectedOfferRevision = session["offerRevision"]!.GetValue<string>() }, 409, "item_trade_not_allowed"))!["session"]!;
                await server.Send(seller, a + "/cancel", new { requestId = Guid.NewGuid(), expectedRevision = session["revision"]!.GetValue<string>() });
            }
            await using (var server = await Server.Start(connection, [], enabled: true))
            {
                var replay = await server.Send(seller, Root(seller) + $"/{listing}/register", original);
                Check(JsonNode.DeepEquals(receipt, replay), "catalog removal/restart replays immutable original registration");
                Check((await items.FindAsync(crystal.ItemId, default))!.State.Quantity == 9, "catalog removal replay does not escrow twice");
                await server.Send(seller, Root(seller) + $"/{Guid.NewGuid()}/register", Register(ore, Guid.NewGuid()), 409, "item_trade_not_allowed");
                await server.Send(seller, Root(seller) + $"/{listing}/cancel", new { requestId = Guid.NewGuid(), expectedRevision = "1" });
                Check((await items.FindAsync(crystal.ItemId, default))!.State.Quantity == 10, "cancel after catalog removal returns original inventory");
            }
            await using (var server = await Server.Start(connection, catalog))
                await server.Send(seller, Root(seller) + $"/{Guid.NewGuid()}/register", Register(ore, Guid.NewGuid()), 404, "auctions_disabled");
            var bad = Entry(); bad["Trading:Items:0:AllowAucton"] = "true";
            await using (var server = await Server.Start(connection, bad, invalid: true))
                Check(server.Output.Contains("Trading:Items:0", StringComparison.Ordinal), "real Program rejects unknown field before starting HTTP");
            Check((await items.FindAsync(ore.ItemId, default))!.Revision == 1 && (await items.FindAsync(newer.ItemId, default))!.Revision == 1
                && (await items.FindAsync(oversize.ItemId, default))!.Revision == 1, "all denied items unchanged across actual Program restarts");
            Console.WriteLine($"PASS item catalog: {checks} assertions; strict startup parsing and actual Program/Player HTTP/PostgreSQL allow/deny/restart/recovery.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private sealed class Server : IAsyncDisposable
    {
        private readonly Process process;
        private readonly ConcurrentQueue<string> lines = new();
        private HttpClient? client;
        public string Output => string.Join(Environment.NewLine, lines);
        private Server(Process process) { this.process = process; }
        public static async Task<Server> Start(string connection, Dictionary<string, string?> catalog, bool? enabled = null, bool invalid = false)
        {
            var start = new ProcessStartInfo("dotnet") { WorkingDirectory = AppContext.BaseDirectory, UseShellExecute = false,
                RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden };
            start.ArgumentList.Add(typeof(TradeItemPolicy).Assembly.Location);
            foreach (string key in start.Environment.Keys.ToArray())
                if (new[] { "Trading__", "Auctions__", "Auth__", "Steam__", "Storage__", "ConnectionStrings__", "Kestrel__" }.Any(prefix => key.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    || key is "POSTGRES_CONNECTION_STRING" or "PROJECT_RPG_BACKEND_ADMIN_TOKEN" or "STEAM_WEB_API_PUBLISHER_KEY") start.Environment.Remove(key);
            foreach (var (key, value) in new[] { ("ASPNETCORE_ENVIRONMENT", "Testing"), ("DOTNET_ENVIRONMENT", "Testing"), ("Storage__Provider", "Postgres"),
                ("ConnectionStrings__Postgres", connection), ("Auth__AdminToken", Guid.NewGuid().ToString("N")), ("Steam__AllowDevelopmentAuthentication", "true"),
                ("RateLimits__Enabled", "false"), ("Kestrel__Endpoints__Http__Url", "http://127.0.0.1:0"),
                ("Logging__LogLevel__Default", "Warning"), ("Logging__LogLevel__Microsoft.Hosting.Lifetime", "Information") }) start.Environment[key] = value;
            if (enabled.HasValue) { start.Environment["Trading__Enabled"] = enabled.Value.ToString(); start.Environment["Auctions__Enabled"] = enabled.Value.ToString(); }
            foreach (var pair in catalog) start.Environment[pair.Key.Replace(":", "__")] = pair.Value;
            var process = new Process { StartInfo = start, EnableRaisingEvents = true }; var server = new Server(process);
            var listening = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
            void Observe(object sender, DataReceivedEventArgs e)
            {
                if (e.Data is not { } line) return; server.lines.Enqueue(line); while (server.lines.Count > 40) server.lines.TryDequeue(out _);
                const string marker = "Now listening on: "; int at = line.IndexOf(marker, StringComparison.Ordinal);
                if (at >= 0) listening.TrySetResult(line[(at + marker.Length)..].Trim());
            }
            process.OutputDataReceived += Observe; process.ErrorDataReceived += Observe;
            process.Exited += (_, _) => listening.TrySetException(new Exception("Own backend exited before listening"));
            try
            {
                Check(process.Start(), "actual Program child starts"); process.BeginOutputReadLine(); process.BeginErrorReadLine();
                if (invalid)
                {
                    await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(20));
                    Check(process.ExitCode != 0 && !listening.Task.IsCompletedSuccessfully, "invalid catalog prevents normal startup");
                }
                else server.client = new HttpClient { BaseAddress = new Uri(await listening.Task.WaitAsync(TimeSpan.FromSeconds(30))) };
                return server;
            }
            catch { await server.DisposeAsync(); throw; }
        }
        public async Task<JsonNode?> Send(Person person, string route, object payload, int status = 200, string? error = null)
        {
            using var request = new HttpRequestMessage(HttpMethod.Post, route) { Content = JsonContent.Create(payload) };
            request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", person.Token);
            using var response = await client!.SendAsync(request); string body = await response.Content.ReadAsStringAsync();
            Check((int)response.StatusCode == status, $"{route} expected {status}, got {(int)response.StatusCode}: {body}");
            var result = JsonNode.Parse(body); if (error is not null) Check((result!["error"] ?? result["rejection"])?.GetValue<string>() == error, $"{route} expected error {error}: {body}");
            return result;
        }
        public async ValueTask DisposeAsync()
        {
            client?.Dispose(); if (!process.HasExited) { process.Kill(); await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10)); } process.Dispose();
        }
    }
}
