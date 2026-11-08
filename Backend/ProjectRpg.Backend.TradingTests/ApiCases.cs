using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Operations.ServiceControl;
using ProjectRpg.Backend.Operations.Restrictions;
using ProjectRpg.Operations.Core.ServiceControl;
using ProjectRpg.Operations.Core.Restrictions;

internal static partial class ApiCases
{
    private static int checks, sequence;
    private static readonly TradeItemPolicy Policy = new([new("Material", "Api.Ore", 1, true, false, 100)]);
    public static async Task<int> Run(string mode)
    {
        try
        {
            await using var f = await Fixture.Create(mode != "--api-memory");
            if (mode == "--api-restart") { await ProgramRestart(f); }
            else { await TargetCases(f); await PreviewCases(f); await Contract(f); await AccessBoundaries(f); await Expiry(f); await Limits(f); }
            Console.WriteLine($"PASS {mode}: {checks} assertions; actual bearer HTTP, isolated repositories/DB."); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }

    private static async Task Contract(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); var eve = await f.Person();
        var item = await f.Item(a); var trade = Guid.NewGuid(); var invite = new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id };
        string ar = Route(a, trade), br = Route(b, trade), er = Route(eve, trade);
        await Send(f, null, ar + "/invite", invite, 401); await Send(f, f.Admin, ar + "/invite", invite, 403);
        await Send(f, eve.Token, ar + "/invite", invite, 403);
        f.Options.Enabled = false; await Send(f, a.Token, ar + "/invite", invite, 404); f.Options.Enabled = true;
        var first = await Send(f, a.Token, ar + "/invite", invite);
        Check(JsonNode.DeepEquals(first, await Send(f, a.Token, ar + "/invite", invite)), "invite replay");
        var s = first["session"]!.AsObject();
        Check(s["currencyCode"]!.GetValue<string>() == "Gold", "server currency");
        Check(JsonNode.DeepEquals(s, (await Get(f, b.Token, Route(b)))!["session"]), "invitee discovers own pending trade");
        await Get(f, eve.Token, er, 404); await Get(f, eve.Token, ar, 403);
        await Send(f, eve.Token, er + "/accept", Command(s), 404);
        await Send(f, a.Token, ar + "/accept", Command(s), 409);
        s = (await Send(f, b.Token, br + "/accept", Command(s)))["session"]!.AsObject();
        await InvalidBodies(f, a, ar, s);
        var large = new { requestId = Guid.NewGuid(), expectedRevision = Rev(s), offer = new { items = Array.Empty<object>(), currencyAmount = "9007199254740993" } };
        s = (await Send(f, a.Token, ar + "/offer", large))["session"]!.AsObject();
        Check(s["initiatorOffer"]!["currencyAmount"]!.GetValue<string>() == "9007199254740993", "exact integer wire value above double precision");
        var offer = new { requestId = Guid.NewGuid(), expectedRevision = Rev(s), offer = new
            { items = new[] { new { itemId = item.ItemId, expectedRevision = "1", quantity = 3 } }, currencyAmount = "20" } };
        s = (await Send(f, a.Token, ar + "/offer", offer))["session"]!.AsObject();
        s = (await Send(f, a.Token, ar + "/ready", Consent(s)))["session"]!.AsObject();
        s = (await Send(f, b.Token, br + "/ready", Consent(s)))["session"]!.AsObject();
        Check(await f.Balance(a) == 100 && (await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 10, "ready reserves without spending");
        await Send(f, a.Token, ar + "/confirm", new { requestId = Guid.NewGuid(), expectedRevision = "1", expectedOfferRevision = "1" }, 409);
        s = (await Send(f, a.Token, ar + "/confirm", Consent(s)))["session"]!.AsObject();
        var final = Consent(s); var completed = await Send(f, b.Token, br + "/confirm", final);
        Check(completed["session"]!["completed"]!.GetValue<bool>(), "HTTP completion");
        Check(await f.Balance(a) == 80 && await f.Balance(b) == 120, "actual money once");
        var received = await f.Items.FindByOwnerAsync(new(ItemOwnerType.Character, b.Id.ToString("D")), false, 100, default);
        Check(received.Any(i => i.DefinitionName == "Api.Ore" && i.State.Quantity == 3), "actual split item received");
        Check(JsonNode.DeepEquals(completed, await Send(f, b.Token, br + "/confirm", final)), "lost-response retry exact");
        await Send(f, b.Token, br + "/cancel", new { requestId = final.requestId, expectedRevision = final.expectedRevision }, 409);
        Check((await Get(f, b.Token, Route(b)))!["session"] is null, "no active completed trade");
        string wire = completed.ToJsonString();
        Check(!wire.Contains(a.Account) && !wire.Contains(b.Account) && !wire.Contains("reservation", StringComparison.OrdinalIgnoreCase)
            && !wire.Contains("exchange", StringComparison.OrdinalIgnoreCase) && !wire.Contains("owner", StringComparison.OrdinalIgnoreCase), "private storage details omitted");
        f.Options.Enabled = false;
        Check((await Get(f, a.Token, ar))!["session"]!["completed"]!.GetValue<bool>(), "read result when new trading disabled"); f.Options.Enabled = true;
        await Send(f, a.Token, ar + "/expire", Command(s), 404);
        if (f.Data is not null)
        {
            await using var c = await f.Data.OpenConnectionAsync();
            await using var q = new NpgsqlCommand("CREATE TABLE api_test_replay(token text, route text, command jsonb, result jsonb); INSERT INTO api_test_replay VALUES(@t,@r,@c,@v)", c);
            q.Parameters.AddWithValue("t", b.Token); q.Parameters.AddWithValue("r", br + "/confirm");
            q.Parameters.AddWithValue("c", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(final)); q.Parameters.AddWithValue("v", NpgsqlDbType.Jsonb, completed.ToJsonString());
            await q.ExecuteNonQueryAsync();
        }
        Console.WriteLine("PASS player-only ownership, bounded DTO, consent/settlement, recovery and private response contract");
    }

    private static async Task InvalidBodies(Fixture f, Person a, string route, JsonObject s)
    {
        var valid = JsonSerializer.Serialize(Command(s));
        foreach (string extra in new[] { "\"actor\":{}", "\"currencyCode\":\"Evil\"", "\"expiresAt\":\"2099-01-01\"", "\"requestId\":\"" + Guid.NewGuid() + "\"" })
            await Raw(f, a.Token, route + "/cancel", valid[..^1] + "," + extra + "}", 400);
        await Raw(f, a.Token, route + "/cancel", "{", 400);
        await Raw(f, a.Token, route + "/cancel", "{}", 415, "text/plain");
        await Raw(f, a.Token, route + "/cancel", new string(' ', TradeApiEndpoints.MaximumBodyBytes + 1), 413);
        await Raw(f, a.Token, route + "/cancel", new string(' ', TradeApiEndpoints.MaximumBodyBytes + 1), 413, chunked: true);
        await Send(f, a.Token, route + "/cancel", new { requestId = Guid.NewGuid(), expectedRevision = 2 }, 400);
        await Send(f, a.Token, route + "/cancel", new { requestId = Guid.NewGuid(), expectedRevision = "02" }, 400);
        await Send(f, a.Token, route + "/ready", Command(s), 400);
        await Send(f, a.Token, route + "/offer", new { requestId = Guid.NewGuid(), expectedRevision = Rev(s),
            offer = new { items = Enumerable.Repeat(new { itemId = Guid.NewGuid(), expectedRevision = "1", quantity = 1 }, 9), currencyAmount = "0" } }, 400);
        await Send(f, a.Token, "/api/item-transactions/commit", new { }, 403);
    }

    private static async Task Expiry(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); var trade = Guid.NewGuid();
        var s = (await Send(f, a.Token, Route(a, trade) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id }))["session"]!.AsObject();
        s = (await Send(f, b.Token, Route(b, trade) + "/accept", Command(s)))["session"]!.AsObject();
        s = (await Send(f, a.Token, Route(a, trade) + "/offer", new { requestId = Guid.NewGuid(), expectedRevision = Rev(s),
            offer = new { items = Array.Empty<object>(), currencyAmount = "25" } }))["session"]!.AsObject();
        s = (await Send(f, a.Token, Route(a, trade) + "/ready", Consent(s)))["session"]!.AsObject();
        await f.MakeExpired(trade);
        Check((await Get(f, b.Token, Route(b)))!["session"] is null, "expired invite absent from active query before worker");
        var worker = new TradeExpiryWorker(f.Store, NullLogger<TradeExpiryWorker>.Instance);
        await Task.WhenAll(Task.Run(() => worker.SweepAsync(default)), Task.Run(() => worker.SweepAsync(default)));
        Check((await Get(f, a.Token, Route(a, trade)))!["session"]!["status"]!.GetValue<string>() == "Expired", "worker closes expired state");
        Check(await worker.SweepAsync(default) == 0 && await f.Balance(a) == 100, "idempotent sweep and no asset loss");
        var spend = await f.Economy.CommitAsync(new(Guid.NewGuid(), a.Id, Guid.NewGuid(), "Test.ExpireSpend", "expire-spend", "fixture", [new("Gold", -100)]),
            (await f.Game.GetCharacterEconomyContextAsync(a.Id, default))!, f.Clock.Now, default);
        Check(spend.Status == CurrencyTransactionStatus.Committed, "expired hold no longer prevents full spend");
        if (f.Data is not null)
        {
            // Leave a due row for the real Program's hosted worker in the separate-process test.
            var x = await f.Person(); var y = await f.Person(); var pending = await f.Store.ExecuteAsync(new(Guid.NewGuid(), Guid.NewGuid(),
                new(x.Account, x.Id), TradeCommandKind.Invite, Invitee: new(y.Account, y.Id), CurrencyCode: "Gold"), default);
            await f.MakeExpired(pending.Session.State.TradeId);
        }
        Console.WriteLine("PASS bounded expiry scan, concurrent workers, atomic reservation release");
    }

    private static async Task AccessBoundaries(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person(); var c = await f.Person();
        async Task<(Guid Id, JsonObject State)> Open()
        {
            var id = Guid.NewGuid(); var s = (await Send(f, a.Token, Route(a,id)+"/invite", new {requestId=Guid.NewGuid(),inviteeCharacterId=b.Id}))["session"]!.AsObject();
            s = (await Send(f,b.Token,Route(b,id)+"/accept",Command(s)))["session"]!.AsObject();
            s = (await Send(f,a.Token,Route(a,id)+"/offer",new {requestId=Guid.NewGuid(),expectedRevision=Rev(s),offer=new {items=Array.Empty<object>(),currencyAmount="20"}}))["session"]!.AsObject();
            return(id,(await Send(f,a.Token,Route(a,id)+"/ready",Consent(s)))["session"]!.AsObject());
        }
        var (id,s) = await Open(); await f.Mode("Drain");
        var refused=await Send(f,b.Token,Route(b,id)+"/ready",Consent(s),409);
        Check(refused["rejection"]!.GetValue<string>()=="service_draining"&&!refused["session"]!["initiatorReady"]!.GetValue<bool>(),"Drain refusal persists released consent");
        await Send(f,a.Token,Route(a,id)+"/cancel",Command(refused["session"]!.AsObject()));
        await Send(f,a.Token,Route(a,Guid.NewGuid())+"/invite",new {requestId=Guid.NewGuid(),inviteeCharacterId=b.Id},503); await f.Mode("Open");
        (id,s)=await Open(); await f.Restrict(b);
        await Get(f,b.Token,Route(b,id),401);
        refused=await Send(f,a.Token,Route(a,id)+"/ready",Consent(s),409);
        Check(refused["rejection"]!.GetValue<string>()=="account_restricted","peer restriction rechecked under transaction");
        await Send(f,a.Token,Route(a,id)+"/cancel",Command(refused["session"]!.AsObject()));
        var dungeon=await f.Game.CreateDungeonSessionAsync(c.Account,c.Id,"ApiFixture","Normal",f.Clock.Now,f.Clock.Now.AddHours(1),default);
        await f.Game.AssignDungeonServerAsync(dungeon.DungeonSessionId,"api-fixture-server","127.0.0.1:1",f.Clock.Now,f.Clock.Now.AddMinutes(10),default);
        var credential=new AccessTokenService(f.Clock).Issue(TimeSpan.FromMinutes(10));
        await f.Game.StoreGameServerCredentialAsync(credential.TokenHash,"api-fixture-server",dungeon.DungeonSessionId,credential.ExpiresAt,default);
        await Send(f,credential.Token,Route(a,Guid.NewGuid())+"/invite",new {requestId=Guid.NewGuid(),inviteeCharacterId=c.Id},403);
        await Send(f,c.Token,Route(c,Guid.NewGuid())+"/invite",new {requestId=Guid.NewGuid(),inviteeCharacterId=a.Id},409);
        Console.WriteLine("PASS real game-server credential denial, admission lease, Drain, restriction and cancellation");
    }

    private static async Task Limits(Fixture f)
    {
        var a = await f.Person(); var b = await f.Person();
        for (int i = 0; i < 5; i++)
        {
            var id = Guid.NewGuid(); var s = (await Send(f, a.Token, Route(a, id) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id }))["session"]!.AsObject();
            await Send(f, a.Token, Route(a, id) + "/cancel", Command(s));
        }
        var blocked = await Send(f, a.Token, Route(a, Guid.NewGuid()) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id }, 429);
        Check(blocked["error"]!.GetValue<string>() == "trade_rate_limit", "actor invitation budget");
        // Same account, different character and token still shares the invitation budget.
        var alt = await f.Person(a.Account);
        await Send(f, alt.Token, Route(alt, Guid.NewGuid()) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id }, 429);
        var target = await f.Person();
        for (int i = 0; i < 11; i++)
        {
            var sender = await f.Person(); var id = Guid.NewGuid();
            var r = await Send(f, sender.Token, Route(sender, id) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = target.Id }, i == 10 ? 429 : 200);
            if (i < 10) await Send(f, target.Token, Route(target, id) + "/cancel", Command(r["session"]!.AsObject()));
        }
        Console.WriteLine("PASS per-account invitation/recipient limits, alts share budget, cancel remains available");
    }

    private static async Task ProgramRestart(Fixture f)
    {
        await using var c = await f.Data!.OpenConnectionAsync();
        await using var q = new NpgsqlCommand("SELECT token,route,command::text,result::text FROM api_test_replay", c);
        string token, route, command, expected;
        await using (var r = await q.ExecuteReaderAsync()) { Check(await r.ReadAsync(), "previous process fixture"); token = r.GetString(0); route = r.GetString(1); command = r.GetString(2); expected = r.GetString(3); }
        // Real production Program registration/middleware/hosted worker, with development authentication isolated to this child.
        ProcessStartInfo start = new("dotnet") { WorkingDirectory = AppContext.BaseDirectory, UseShellExecute = false,
            RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
        start.ArgumentList.Add(typeof(TradeApplication).Assembly.Location); start.ArgumentList.Add("--urls"); start.ArgumentList.Add("http://127.0.0.1:0");
        foreach (var (key, value) in new[] { ("ASPNETCORE_ENVIRONMENT", "Testing"), ("DOTNET_ENVIRONMENT", "Testing"),
            ("Storage__Provider", "Postgres"), ("ConnectionStrings__Postgres", Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES")!),
            ("POSTGRES_CONNECTION_STRING", Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES")!),
            ("Auth__AdminToken", Guid.NewGuid().ToString("N")), ("Steam__AllowDevelopmentAuthentication", "true"),
            ("DungeonContent__DevelopmentProfile", "Training"), ("Trading__Enabled", "true"), ("RateLimits__Enabled", "false"),
            ("Kestrel__Endpoints__Http__Url", "http://127.0.0.1:0"),
            ("Logging__LogLevel__Default", "Warning"), ("Logging__LogLevel__Microsoft.Hosting.Lifetime", "Information") }) start.Environment[key] = value;
        TaskCompletionSource<string> address = new(TaskCreationOptions.RunContinuationsAsynchronously); ConcurrentQueue<string> tail = new();
        start.Environment.Remove("PROJECT_RPG_BACKEND_ADMIN_TOKEN");
        start.Environment.Remove("STEAM_WEB_API_PUBLISHER_KEY");
        void Observe(object sender, DataReceivedEventArgs line) { if (line.Data is not string value) return; tail.Enqueue(value); while(tail.Count > 40) tail.TryDequeue(out _); const string marker = "Now listening on: "; int at = value.IndexOf(marker, StringComparison.Ordinal); if(at >= 0) address.TrySetResult(value[(at+marker.Length)..]); }
        using Process process = new() { StartInfo = start, EnableRaisingEvents = true }; process.OutputDataReceived += Observe; process.ErrorDataReceived += Observe;
        process.Exited += (_, _) => address.TrySetException(new InvalidOperationException("Own backend exited before reporting its listener."));
        Check(process.Start(), "own Program process starts"); process.BeginOutputReadLine(); process.BeginErrorReadLine();
        try
        {
            var url = await address.Task.WaitAsync(TimeSpan.FromSeconds(30)); Check(new Uri(url).IsLoopback, "loopback child");
            using HttpClient client = new() { BaseAddress = new(url) }; client.DefaultRequestHeaders.Authorization = new("Bearer", token);
            using var response = await client.PostAsync(route, new StringContent(command, Encoding.UTF8, "application/json"));
            Check(response.IsSuccessStatusCode && JsonNode.DeepEquals(JsonNode.Parse(expected), JsonNode.Parse(await response.Content.ReadAsStringAsync())), "new Program process returns exact persisted HTTP receipt");
            for (int i = 0; i < 80 && (await f.Store.FindExpiredAsync(100, default)).Count != 0; i++) await Task.Delay(100);
            Check((await f.Store.FindExpiredAsync(100, default)).Count == 0, "actual Program hosted worker closes pending expiry");
        }
        catch { Console.Error.WriteLine(string.Join(Environment.NewLine, tail)); throw; }
        finally { if (!process.HasExited) process.Kill(entireProcessTree: true); await process.WaitForExitAsync(); }
        Console.WriteLine("PASS separate Program process DI/auth/HTTP receipt recovery and hosted expiry");
    }

    private static string Route(Person p, Guid? id = null) => $"/api/characters/{p.Id:D}/trades" + (id.HasValue ? $"/{id:D}" : "");
    private static string Rev(JsonObject s) => s["revision"]!.GetValue<string>();
    private sealed record CommandBody(Guid requestId, string expectedRevision, string? expectedOfferRevision = null);
    private static CommandBody Command(JsonObject s) => new(Guid.NewGuid(), Rev(s));
    private static CommandBody Consent(JsonObject s) => new(Guid.NewGuid(), Rev(s), s["offerRevision"]!.GetValue<string>());
    private static async Task<JsonObject> Send(Fixture f, string? token, string route, object payload, int status = 200)
    { using var request = new HttpRequestMessage(HttpMethod.Post, route) { Content = JsonContent.Create(payload) }; return await Response(f, token, request, status); }
    private static async Task<JsonObject?> Get(Fixture f, string token, string route, int status = 200)
    { using var request = new HttpRequestMessage(HttpMethod.Get, route); return await Response(f, token, request, status); }
    private static async Task Raw(Fixture f, string token, string route, string body, int status, string type = "application/json", bool chunked = false)
    { using var request = new HttpRequestMessage(HttpMethod.Post, route) { Content = new StringContent(body, Encoding.UTF8, type) }; if(chunked) request.Headers.TransferEncodingChunked = true; await Response(f, token, request, status); }
    private static async Task<JsonObject> Response(Fixture f, string? token, HttpRequestMessage request, int status)
    {
        if(token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
        using var response = await f.Client.SendAsync(request); var text = await response.Content.ReadAsStringAsync();
        Check((int)response.StatusCode == status, $"{request.RequestUri} expected {status}, got {(int)response.StatusCode}: {text}");
        if(status == 200 || status == 409 || status == 429) Check(response.Headers.CacheControl?.NoStore == true, "uncacheable trade response");
        if(status == 429) Check(response.Headers.RetryAfter?.Delta == TimeSpan.FromSeconds(60), "retry-after");
        return string.IsNullOrEmpty(text) ? new() : JsonNode.Parse(text)!.AsObject();
    }
    private static void Check(bool value, string message) { if(!value) throw new Exception(message); checks++; }
    private sealed record Person(string Account, Guid Id, string Token);
    private sealed class TestClock : TimeProvider { public DateTimeOffset Now = DateTimeOffset.UtcNow; public override DateTimeOffset GetUtcNow() => Now; }
    private sealed class Fixture : IAsyncDisposable
    {
        public NpgsqlDataSource? Data; public TestClock Clock = new(); public IGameRepository Game = null!;
        public IItemRepository Items = null!; public IEconomyRepository Economy = null!; public ITradeSessionRepository Store = null!;
        public TradingOptions Options = new() { Enabled = true }; public string Admin = Guid.NewGuid().ToString("N");
        public HttpClient Client = null!; private WebApplication app = null!;
        public static async Task<Fixture> Create(bool pg)
        {
            var f = new Fixture();
            if(pg)
            {
                var cs = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_EQUIPMENT_TEST_POSTGRES"));
                if(cs.Host != "127.0.0.1" || cs.Database != "equipment_test" || !Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_RUN"), "N", out _)) throw new Exception("Private database required");
                f.Data = NpgsqlDataSource.Create(cs.ConnectionString); f.Game = new PostgresGameRepository(f.Data, NullLogger<PostgresGameRepository>.Instance);
                f.Items = new PostgresItemRepository(f.Data, NullLogger<PostgresItemRepository>.Instance); f.Economy = new PostgresEconomyRepository(f.Data); f.Store = new PostgresTradeSessionRepository(f.Data, Policy);
            }
            else { var gate = new InMemoryTransactionGate(f.Clock); var items = new InMemoryItemRepository(gate); var money = new InMemoryEconomyRepository(gate); var game = new InMemoryGameRepository(gate, items, f.Clock); f.Items = items; f.Economy = money; f.Game = game; f.Store = new InMemoryTradeSessionRepository(gate, game, items, money, Policy); }
            await f.Game.EnsureCreatedAsync(default); await f.Economy.UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Account, long.MaxValue, true), f.Clock.Now, default);
            var b = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing", ContentRootPath = AppContext.BaseDirectory }); b.Logging.ClearProviders(); b.WebHost.UseUrls("http://127.0.0.1:0");
            b.Services.AddSingleton(f.Game); b.Services.AddSingleton(f.Items); b.Services.AddSingleton(f.Store); b.Services.AddSingleton<TimeProvider>(f.Clock);
            b.Services.AddSingleton<AccessTokenService>(); b.Services.AddSingleton<IOptions<AuthOptions>>(Microsoft.Extensions.Options.Options.Create(new AuthOptions { AdminToken = f.Admin }));
            b.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Microsoft.Extensions.Options.Options.Create(new SecurityTelemetryOptions()));
            b.Services.AddSingleton<IOptions<TradingOptions>>(Microsoft.Extensions.Options.Options.Create(f.Options)); b.Services.AddSingleton<TradeRequestLimiter>(); b.Services.AddSingleton<TradeApplication>(); b.Services.AddScoped<BearerAuthenticator>();
            b.Services.AddGameRepositoryContracts(); b.Services.AddSingleton<ItemOwnerAccessService>(); b.Services.AddSingleton<ItemTransactionService>();
            f.app = b.Build(); f.app.UseMiddleware<ApiAuthenticationMiddleware>(); f.app.MapTradeApi(); f.app.MapItemApi(); await f.app.StartAsync();
            var address = f.app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single(); f.Client = new() { BaseAddress = new(address), Timeout = TimeSpan.FromSeconds(15) }; return f;
        }
        public async Task<Person> Person(string? account = null)
        {
            int n = Interlocked.Increment(ref sequence); account ??= (76561900000000000L+n).ToString(); await Game.UpsertAccountAsync(new(account, account, false, false), Clock.Now, default);
            var c = await Game.CreateCharacterAsync(account, "ApiTrade" + n, Clock.Now, default); var token = new AccessTokenService(Clock).Issue(TimeSpan.FromHours(1)); await Game.StoreSessionAsync(token.TokenHash, account, token.ExpiresAt, default);
            var result = await Economy.CommitAsync(new(Guid.NewGuid(), c.CharacterId, Guid.NewGuid(), "Test.Fund", Guid.NewGuid().ToString("N"), "test", [new("Gold", 100)]), (await Game.GetCharacterEconomyContextAsync(c.CharacterId, default))!, Clock.Now, default);
            Check(result.Status == CurrencyTransactionStatus.Committed, "seed balance"); return new(account, c.CharacterId, token.Token);
        }
        public async Task<ItemRecord> Item(Person p)
        {
            var owner = new ItemOwnerRef(ItemOwnerType.Character, p.Id.ToString("D")); ItemRecord item = new("Material", "Api.Ore", 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, 30), new(Guid.NewGuid(), 1, 10, [], []), 0, ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0,0), null, "Test", false));
            var result = await Items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "fixture", owner, 10, [new(0, item)]), Clock.Now, default); Check(result.Status == ItemRepositoryCommitStatus.Committed, "seed item"); return result.Records.Single();
        }
        public async Task<long> Balance(Person p) => (await Economy.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(p.Id, default))!, default)).Balances.Single(b => b.Definition.CurrencyCode == "Gold").Balance;
        public async Task MakeExpired(Guid id)
        {
            if(Data is null) { Clock.Now += TimeSpan.FromMinutes(6); return; }
            await using var c = await Data.OpenConnectionAsync(); await using var q = new NpgsqlCommand("UPDATE trade_sessions SET expires_at=clock_timestamp()-interval '1 second', payload=jsonb_set(payload,'{State,ExpiresAt}',to_jsonb(clock_timestamp()-interval '1 second')) WHERE trade_id=@id", c); q.Parameters.AddWithValue("id", id); Check(await q.ExecuteNonQueryAsync() == 1, "controlled due session fixture");
        }
        public async Task Mode(string mode)
        {
            IServiceControlReader reader; IServiceControlCommitter writer;
            if(Data is null){var store=new MemoryServiceControlStore((InMemoryGameRepository)Game,Clock);reader=store;writer=store;}
            else{var store=new PostgresServiceControlStore(Data,Clock);reader=store;writer=store;}
            var snapshot=await reader.ReadAsync(default);var result=await writer.CommitAsync(new(new(Guid.NewGuid(),snapshot.Value!.Revision,mode,"",null,null,"controlled trade API fixture"),"test",Guid.NewGuid(),null),default);
            Check(result.IsSuccess,"service mode fixture");
        }
        public async Task Restrict(Person p)
        {
            IAccountRestrictionCommitter store=Data is null?new MemoryRestrictionStore((InMemoryGameRepository)Game,Clock):new PostgresRestrictionStore(Data,Clock);
            var result=await store.CommitAsync(new(new(Guid.NewGuid(),p.Account,0,null,"controlled trade API fixture"),"test",Guid.NewGuid(),null),default);Check(result.IsSuccess,"restriction fixture");
        }
        public async ValueTask DisposeAsync() { Client.Dispose(); await app.StopAsync(); await app.DisposeAsync(); if(Data is not null) await Data.DisposeAsync(); }
    }
}
