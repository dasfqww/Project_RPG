using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

try
{
    Func<IGameRepository> memory = () => new InMemoryGameRepository();
    Func<IItemRepository> memoryItems = () => new InMemoryItemRepository(new InMemoryTransactionGate());
    if (args.Contains("--item-revision-only"))
    {
        if (args.Contains("--item-ingress-only") || args.Contains("--item-replay-only")
            || args.Contains("--item-replay-postgres-only") || args.Contains("--require-postgres"))
        {
            Console.Error.WriteLine("Item revision-only checks use domain/service and HTTP/Memory; do not combine other modes.");
            return 2;
        }
        await ItemCommitReplayChecks.RunRevision(memoryItems);
        return 0;
    }
    if (args.Contains("--item-ingress-only"))
    {
        if (args.Contains("--item-replay-only") || args.Contains("--item-replay-postgres-only") || args.Contains("--require-postgres"))
        {
            Console.Error.WriteLine("Item ingress-only checks use HTTP/Memory; do not combine replay or required-PostgreSQL modes.");
            return 2;
        }
        await ItemCommitReplayChecks.RunIngress(memoryItems);
        return 0;
    }
    if (args.Contains("--item-replay-postgres-only"))
    {
        if (args.Contains("--item-replay-only"))
        {
            Console.Error.WriteLine("Choose either Memory-only or PostgreSQL-only item replay checks, not both.");
            return 2;
        }
        string? replayConnection = Environment.GetEnvironmentVariable("PROJECT_RPG_PICKUP_INTENT_TEST_POSTGRES");
        if (string.IsNullOrWhiteSpace(replayConnection))
        {
            Console.Error.WriteLine("PostgreSQL item replay checks require an explicit local test connection; no Memory fallback.");
            return 2;
        }
        await using var replaySchema = await PrivateSchema.Create(replayConnection);
        await ItemCommitReplayChecks.RunPostgres(replaySchema);
        return 0;
    }
    if (args.Contains("--item-replay-only"))
    {
        if (args.Contains("--require-postgres"))
        {
            Console.Error.WriteLine("Item replay-only checks cover Memory/HTTP, not PostgreSQL; incompatible --require-postgres.");
            return 2;
        }
        await ItemCommitReplayChecks.Run(memoryItems);
        return 0;
    }
    await Checks.Suite("Memory", memory, memoryItems);
    await CommitChecks.Suite("Memory", memory, memoryItems);
    string? connection = Environment.GetEnvironmentVariable("PROJECT_RPG_PICKUP_INTENT_TEST_POSTGRES");
    if (string.IsNullOrWhiteSpace(connection))
    {
        Console.WriteLine("SKIP PostgreSQL: loopback test connection not configured.");
        return args.Contains("--require-postgres") ? 2 : 0;
    }
    await using var schema = await PrivateSchema.Create(connection);
    await Checks.Suite("Postgres", schema.Repository, schema.Items);
    await Checks.Database(schema);
    await CommitChecks.Suite("Postgres", schema.Repository, schema.Items);
    await CommitChecks.Database(schema);
    Console.WriteLine("PASS pickup-intent and atomic-commit focused tests. No UE/BP/replay/Steam verification claimed.");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine("FAIL PickupIntent: " + (exception is TestFailure ? exception.Message
        : exception is PostgresException pg ? "SQLSTATE " + pg.SqlState : exception.GetType().Name));
    return 1;
}

static class Checks
{
    public static void Require(bool value, string message) { if (!value) throw new TestFailure(message); }
    public static async Task Suite(string label, Func<IGameRepository> create, Func<IItemRepository> createItems)
    {
        int passed = 0;
        async Task Test(string name, Func<Fixture, Task> action)
        {
            await action(await Fixture.Create(create(), items: createItems()));
            Console.WriteLine($"PASS {label}: {name}"); passed++;
        }
        await Test("prepare and exact retry freeze full command", async f =>
        {
            var first = await f.Prepare();
            var second = await f.Prepare();
            Require(first.Status == DungeonPickupIntentStatus.Prepared && second.Status == DungeonPickupIntentStatus.AlreadyPrepared
                && first.Intent == second.Intent && first.Intent!.PreparedAt == f.Now, "Reservation/retry not immutable.");
            Require((await f.Read()).Intent == first.Intent, "Stored command changed.");
        });
        await Test("same fingerprint cannot conceal a changed seed or destination", async f =>
        {
            await f.Prepare();
            var mutation = f.Command.Mutations[0];
            foreach (var record in new[] { mutation.NewRecord with { State = mutation.NewRecord.State with { GenerationSeed = 8 } },
                mutation.NewRecord with { Location = mutation.NewRecord.Location with { SlotIndex = 1 } } })
                Require((await f.Prepare(command: f.Command with { Mutations = [new(0, record)] })).Status == DungeonPickupIntentStatus.Conflict,
                    "Changed body reused declared fingerprint.");
        });
        await Test("one drop cannot bind another request or member", async f =>
        {
            await f.Prepare();
            Require((await f.Prepare(command: Fixture.CommandFor(f.B))).Status == DungeonPickupIntentStatus.Conflict, "Drop rebound to another claimant.");
        });
        await Test("request and item IDs are unique beyond a drop", async f =>
        {
            await f.Prepare();
            Require((await f.Prepare(drop: Guid.NewGuid())).Status == DungeonPickupIntentStatus.Conflict, "Request rebound to another drop.");
            Require((await f.Prepare(drop: Guid.NewGuid(), command: f.Command with { RequestId = Guid.NewGuid() })).Status == DungeonPickupIntentStatus.Conflict,
                "Item rebound to another request/drop.");
        });
        await Test("changed command input cannot mutate an existing snapshot", async f =>
        {
            var list = new List<ItemRecordMutation>(f.Command.Mutations);
            var first = await f.Prepare(command: f.Command with { Mutations = list });
            list.Clear();
            Require((await f.Read()).Intent == first.Intent, "Mutable input damaged snapshot.");
        });
        await Test("wrong assigned server and outsider fail without data", async f =>
        {
            await f.Prepare();
            var wrong = await f.Intents.ReadPickupIntentAsync(f.Session, "another-server", f.Command.RequestId, f.Now, default);
            Require(wrong.Status == DungeonPickupIntentStatus.Forbidden && wrong.Intent is null, "Wrong server exposed intent.");
            Require((await f.Prepare(command: Fixture.CommandFor(f.Outsider))).Status == DungeonPickupIntentStatus.Forbidden, "Outsider prepared a pickup.");
        });
        await Test("expiry rejects prepare, known read and unknown read", async f =>
        {
            await f.Prepare();
            Require((await f.Intents.PreparePickupAsync(f.Session, f.Server, f.Drop, f.Command, f.Expires, default)).Status == DungeonPickupIntentStatus.Forbidden,
                "Expired reservation retry accepted.");
            foreach (var id in new[] { f.Command.RequestId, Guid.NewGuid() })
                Require((await f.Intents.ReadPickupIntentAsync(f.Session, f.Server, id, f.Expires, default)).Status == DungeonPickupIntentStatus.Forbidden, "Expired scope readable.");
        });
        await Test("opt-out preserves origin but cannot recover authority", async f =>
        {
            await f.Prepare();
            await f.Abandon();
            Require((await f.Read()).Status == DungeonPickupIntentStatus.Forbidden && (await f.Prepare()).Status == DungeonPickupIntentStatus.Forbidden,
                "Old prepared command restored removed member authority.");
            Require(await f.Repo.IsAuthorizedGameServerSessionMemberAsync(f.Session, f.Server, f.B.SteamId, f.B.CharacterId, f.Now, default), "Remaining member lost authority.");
        });
        await Test("settlement rejects new and old origins", async f =>
        {
            await f.Prepare();
            await f.Repo.EnqueueDungeonRewardSettlementAsync(f.Session, f.Server, "intent-test", new string('a', 64), [], [], f.Now, default);
            Require((await f.Read()).Status == DungeonPickupIntentStatus.Forbidden && (await f.Prepare()).Status == DungeonPickupIntentStatus.Forbidden,
                "Settlement re-enabled pickup reservation.");
        });
        await Test("failed session cannot expose or reprepare an old origin", async f =>
        {
            await f.Prepare();
            await f.Repo.FinishDungeonSessionAsync(f.Session, f.Server, DungeonSessionState.Failed, f.Now, default);
            Require((await f.Read()).Status == DungeonPickupIntentStatus.Forbidden
                && (await f.Prepare()).Status == DungeonPickupIntentStatus.Forbidden, "Terminal session allowed reservation.");
        });
        await Test("loading is not gameplay pickup authority", async _ =>
        {
            var loading = await Fixture.Create(create(), false);
            Require((await loading.Prepare()).Status == DungeonPickupIntentStatus.Forbidden
                && (await loading.Read()).Status == DungeonPickupIntentStatus.Forbidden, "Loading session allowed pickup preparation.");
        });
        await Test("invalid pickup shapes cannot reserve", async f =>
        {
            Require((await f.Prepare(drop: Guid.Empty)).Status == DungeonPickupIntentStatus.InvalidRequest, "Zero drop allowed.");
            var record = f.Command.Mutations[0].NewRecord;
            foreach (var bad in new[] {
                f.Command with { Operation = "Item.Consume" }, f.Command with { AffectedQuantity = 0 },
                f.Command with { AffectedQuantity = 128, Mutations = [new(0, record with { State = record.State with { Quantity = 128 } })] },
                f.Command with { Mutations = [new(0, record with { Location = record.Location with { SlotIndex = 80 } })] },
                f.Command with { Mutations = [new(0, record with { Owner = new(ItemOwnerType.Account, "bad-owner") })] },
                f.Command with { Mutations = [new(1, record with { Revision = 1 })] } })
                Require((await f.Prepare(command: bad)).Status == DungeonPickupIntentStatus.InvalidRequest, "Invalid creation reserved.");
        });
        await Test("same concurrent request creates one durable origin", async f =>
        {
            var results = await Task.WhenAll(Enumerable.Range(0, 16).Select(_ => Task.Run(() => f.Prepare())));
            Require(results.Count(r => r.Status == DungeonPickupIntentStatus.Prepared) == 1
                && results.Count(r => r.Status == DungeonPickupIntentStatus.AlreadyPrepared) == 15, "Duplicate concurrent reservation.");
        });
        await Test("concurrent party claim freezes only one winner", async f =>
        {
            var results = await Task.WhenAll(Task.Run(() => f.Prepare()), Task.Run(() => f.Prepare(command: Fixture.CommandFor(f.B))));
            Require(results.Count(r => r.Status == DungeonPickupIntentStatus.Prepared) == 1 && results.Count(r => r.Status == DungeonPickupIntentStatus.Conflict) == 1,
                "Both members reserved the same drop.");
        });
        await Test("prepare versus opt-out shares the session lock", async f =>
        {
            var prepare = Task.Run(() => f.Prepare());
            var abandon = Task.Run(f.Abandon);
            await Task.WhenAll(prepare, abandon);
            Require((await prepare).Status is DungeonPickupIntentStatus.Prepared or DungeonPickupIntentStatus.Forbidden,
                "Invalid prepare/opt-out ordering.");
            var expected = (await prepare).Status == DungeonPickupIntentStatus.Prepared
                ? DungeonPickupIntentStatus.Forbidden : DungeonPickupIntentStatus.NotFound;
            Require((await f.Read()).Status == expected, "Late read recovered removed authority.");
        });
        await Test("request identity cannot migrate to a different active session", async f =>
        {
            await f.Prepare();
            var other = await Fixture.Create(f.Repo);
            var rebound = Fixture.CommandFor(other.A) with { RequestId = f.Command.RequestId };
            var result = await other.Prepare(command: rebound);
            Require(result.Status == DungeonPickupIntentStatus.Conflict && result.Intent is null, "Global request scope migrated/leaked.");
        });
        await Test("cancelled request does not prepare", async f =>
        {
            using var cancel = new CancellationTokenSource(); cancel.Cancel();
            try { await f.Intents.PreparePickupAsync(f.Session, f.Server, f.Drop, f.Command, f.Now, cancel.Token); throw new TestFailure("Cancelled write succeeded."); }
            catch (OperationCanceledException) { }
            Require((await f.Read()).Status == DungeonPickupIntentStatus.NotFound, "Cancellation left reservation.");
        });
        await Test("real HTTP authentication, body bounds and idempotency", Http);
        Console.WriteLine($"PASS {label} total: {passed} focused cases.");
    }

    private static async Task Http(Fixture f)
    {
        TimeProvider clock = new FixedClock(f.Now);
        var tokens = new AccessTokenService(clock);
        var player = tokens.Issue(TimeSpan.FromHours(1)); var server = tokens.Issue(TimeSpan.FromHours(1));
        string admin = tokens.Issue(TimeSpan.FromHours(1)).Token;
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        var builder = WebApplication.CreateBuilder(); builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo); builder.Services.AddSingleton(f.Intents);
        builder.Services.AddSingleton(f.Items); builder.Services.AddSingleton<DungeonPickupCommitService>();
        builder.Services.AddSingleton(clock); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapDungeonPickupIntentApi();
        await app.StartAsync();
        using var client = new HttpClient { BaseAddress = new(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
        string path = $"/api/dungeon-sessions/{f.Session:D}/pickup-intents";
        async Task<HttpResponseMessage> Send(string? token, bool get = false, string? body = null, string? target = null, bool chunked = false)
        {
            using var request = new HttpRequestMessage(get ? HttpMethod.Get : HttpMethod.Post, target ?? (get ? path + "/" + f.Command.RequestId : path));
            if (token is not null) request.Headers.Authorization = new("Bearer", token);
            if (chunked) request.Headers.TransferEncodingChunked = true;
            if (!get) request.Content = new StringContent(body ?? JsonSerializer.Serialize(new PrepareDungeonPickupRequest(f.Drop,
                DungeonPickupIntentRules.ToContract(f.Command)), DungeonPickupIntentRules.JsonOptions), Encoding.UTF8, "application/json");
            return await client.SendAsync(request);
        }
        foreach (var (token, status) in new[] { ((string?)null, 401), ("invalid", 401), (player.Token, 403), (admin, 403) })
            foreach (bool get in new[] { false, true })
            {
                using var response = await Send(token, get);
                Require((int)response.StatusCode == status, "HTTP auth boundary.");
            }
        using (var wrong = await Send(server.Token, target: path.Replace(f.Session.ToString("D"), Guid.NewGuid().ToString("D"))))
            Require(wrong.StatusCode == HttpStatusCode.Forbidden, "HTTP server/session mismatch.");
        using (var malformed = await Send(server.Token, body: "{")) Require(malformed.StatusCode == HttpStatusCode.BadRequest, "Malformed body accepted.");
        using (var large = await Send(server.Token, body: new string('x', 128 * 1024 + 1))) Require((int)large.StatusCode == 413, "Body limit bypassed.");
        using (var chunked = await Send(server.Token, body: new string('x', 128 * 1024 + 1), chunked: true)) Require((int)chunked.StatusCode == 413, "Chunked body limit bypassed.");
        using (var first = await Send(server.Token))
        {
            Require(first.StatusCode == HttpStatusCode.OK && first.Headers.CacheControl?.NoStore == true, "Preparation HTTP response.");
            using var body = JsonDocument.Parse(await first.Content.ReadAsStringAsync());
            Require(body.RootElement.GetProperty("status").GetString() == "Prepared"
                && body.RootElement.GetProperty("dropId").GetGuid() == f.Drop
                && body.RootElement.GetProperty("command").GetProperty("requestId").GetGuid() == f.Command.RequestId, "HTTP frozen origin mismatch.");
        }
        using (var retry = await Send(server.Token)) Require((await retry.Content.ReadAsStringAsync()).Contains("AlreadyPrepared"), "HTTP retry not idempotent.");
        using (var read = await Send(server.Token, true)) Require(read.StatusCode == HttpStatusCode.OK, "HTTP origin unreadable.");
        await f.Abandon();
        using (var removed = await Send(server.Token, true)) Require(removed.StatusCode == HttpStatusCode.Forbidden, "HTTP removed member origin readable.");
        await f.Repo.EnqueueDungeonRewardSettlementAsync(f.Session, f.Server, "intent-http", new string('d', 64), [], [], f.Now, default);
        using (var restricted = await Send(server.Token)) Require(restricted.StatusCode == HttpStatusCode.Forbidden, "Restricted bearer reserved pickup.");
        await app.StopAsync();
    }

    public static async Task Database(PrivateSchema schema)
    {
        var f = await Fixture.Create(schema.Repository());
        var stored = await f.Prepare();
        var fresh = (IDungeonPickupIntentRepository)schema.Repository();
        Require((await fresh.ReadPickupIntentAsync(f.Session, f.Server, f.Command.RequestId, f.Now, default)).Intent == stored.Intent,
            "New PG repository/connection lost origin.");
        Require(await schema.Scalar("SELECT count(*) FROM item_records WHERE creation_source <> 'CharacterCreation'") == 0
            && await schema.Scalar("SELECT count(*) FROM item_transaction_receipts") == 0, "Preparation granted items/receipts.");
        await f.Abandon();
        Require(await schema.Scalar("SELECT count(*) FROM dungeon_pickup_intents WHERE request_id = @id", f.Command.RequestId) == 1,
            "Opt-out erased immutable pickup history.");
        Console.WriteLine("PASS Postgres: fresh connection persistence, no item/receipt writes, opt-out history retained.");

        var rollback = await Fixture.Create(schema.Repository());
        await schema.Execute("ALTER TABLE dungeon_pickup_intents ADD CONSTRAINT reject_intent_fixture CHECK (false) NOT VALID");
        try { await rollback.Prepare(); throw new TestFailure("Forced insertion failure ignored."); }
        catch (PostgresException error) when (error.SqlState == "23514") { }
        finally { await schema.Execute("ALTER TABLE dungeon_pickup_intents DROP CONSTRAINT reject_intent_fixture"); }
        Require((await rollback.Read()).Status == DungeonPickupIntentStatus.NotFound, "Failed insert partially persisted.");
        Require((await rollback.Prepare()).Status == DungeonPickupIntentStatus.Prepared, "Failed insert could not recover.");
        Console.WriteLine("PASS Postgres: forced reservation rollback and retry.");

        var locked = await Fixture.Create(schema.Repository());
        await using var connection = await schema.Source.OpenConnectionAsync();
        await using var transaction = await connection.BeginTransactionAsync();
        await using (var parent = new NpgsqlCommand("SELECT 1 FROM dungeon_sessions WHERE dungeon_session_id = @id FOR UPDATE", connection, transaction))
        { parent.Parameters.AddWithValue("id", locked.Session); await parent.ExecuteScalarAsync(); }
        var mutable = new List<ItemRecordMutation>(locked.Command.Mutations);
        var pending = locked.Intents.PreparePickupAsync(locked.Session, locked.Server, locked.Drop,
            locked.Command with { Mutations = mutable }, locked.Now, default);
        bool waited = false;
        for (int i = 0; i < 100 && !waited; i++)
        {
            waited = await schema.WaitingLocks() > 0;
            if (!waited) await Task.Delay(20);
        }
        Require(waited && !pending.IsCompleted, "Prepare did not wait for actual parent lock.");
        mutable.Clear(); // caller mutation during an actual asynchronous DB wait
        await transaction.CommitAsync();
        Require((await pending).Status == DungeonPickupIntentStatus.Prepared
            && (await pending).Intent!.ItemId == locked.Command.Mutations[0].NewRecord.ItemId, "Parent lock waiter/frozen identity failed.");
        Console.WriteLine("PASS Postgres: actual parent FOR UPDATE wait and deep-frozen command keys.");
    }
}

sealed class Fixture(IGameRepository repo, DateTimeOffset now, GameCharacter a, GameCharacter b, GameCharacter outsider,
    Guid session, string server, IItemRepository? items = null)
{
    public IGameRepository Repo => repo;
    public IDungeonPickupIntentRepository Intents => (IDungeonPickupIntentRepository)repo;
    public IItemRepository Items => items ?? throw new InvalidOperationException("Item fixture not configured.");
    public DateTimeOffset Now => now;
    public DateTimeOffset Expires => now.AddHours(1);
    public GameCharacter A => a;
    public GameCharacter B => b;
    public GameCharacter Outsider => outsider;
    public Guid Session => session;
    public string Server => server;
    public Guid Drop { get; } = Guid.NewGuid();
    public ItemRepositoryCommitRequest Command { get; } = CommandFor(a);
    public Task<DungeonPickupIntentResult> Prepare(Guid? drop = null, ItemRepositoryCommitRequest? command = null) =>
        Intents.PreparePickupAsync(Session, Server, drop ?? Drop, command ?? Command, Now, default);
    public Task<DungeonPickupIntentResult> Read() => Intents.ReadPickupIntentAsync(Session, Server, Command.RequestId, Now, default);
    public Task<DungeonAbandonment> Abandon() => Repo.AbandonDungeonSessionAsync(Session, A.SteamId, A.CharacterId, Now, default);
    public DungeonPickupCommitService CommitService(TimeProvider? clock = null) =>
        new(Repo, Items, clock ?? new FixedClock(Now), NullLogger<DungeonPickupCommitService>.Instance);
    public Task<DungeonPickupCommitResult> Commit() => CommitService().CommitAsync(Session, Server, Command.RequestId, default);
    public static ItemRepositoryCommitRequest CommandFor(GameCharacter character)
    {
        string id = character.CharacterId.ToString("D");
        var owner = new ItemOwnerRef(ItemOwnerType.Character, id);
        var record = new ItemRecord("RPGItemDefinition", "GameItem.Equipment.Weapon.GreatSword", 1, owner,
            new(ItemContainerType.Inventory, id, 0), new(Guid.NewGuid(), 7, 1, [], []), 0,
            ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, "WorldPickup", false));
        return new(Guid.NewGuid(), "Item.Pickup", "same-declared-fingerprint", owner, 1, [new(0, record)]);
    }
    public static async Task<Fixture> Create(IGameRepository repo, bool start = true, IItemRepository? items = null)
    {
        var now = new DateTimeOffset(DateTime.UtcNow.Ticks / 10 * 10, TimeSpan.Zero);
        async Task<GameCharacter> Character()
        {
            string steam = BitConverter.ToUInt64(Guid.NewGuid().ToByteArray()).ToString("D20");
            await repo.UpsertAccountAsync(new(steam, steam, false, false), now, default);
            return await repo.CreateCharacterAsync(steam, "P" + Guid.NewGuid().ToString("N")[..18], now, default);
        }
        var a = await Character(); var b = await Character(); var c = await Character();
        var session = await repo.CreateDungeonSessionAsync(a.SteamId, a.CharacterId, "Dungeon.PickupIntent", "Normal", now, now.AddHours(1), default);
        await repo.JoinDungeonSessionAsync(session.DungeonSessionId, b.SteamId, b.CharacterId, 4, now, now.AddHours(1), default);
        string server = "intent-" + Guid.NewGuid().ToString("N");
        await repo.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:7777", now, now.AddHours(1), default);
        if (start) await repo.StartDungeonSessionAsync(session.DungeonSessionId, server, now, now.AddHours(1), default);
        return new(repo, now, a, b, c, session.DungeonSessionId, server, items);
    }
}

sealed class FixedClock(DateTimeOffset now) : TimeProvider { public override DateTimeOffset GetUtcNow() => now; }
sealed class TestFailure(string message) : Exception(message);

sealed class PrivateSchema : IAsyncDisposable
{
    public readonly NpgsqlDataSource Source;
    private readonly NpgsqlDataSource admin;
    private readonly string name = "rpg_pickup_intent_test_" + Guid.NewGuid().ToString("N");
    private bool created;
    private PrivateSchema(string connection)
    {
        var config = new NpgsqlConnectionStringBuilder(connection) { Timeout = 5, CommandTimeout = 30, MaxPoolSize = 20, ApplicationName = name };
        Checks.Require(config.Host == "127.0.0.1" && config.Port == 54329 && config.Database == "project_rpg" && config.Username == "project_rpg", "Tests require the explicit local development database.");
        config.SearchPath = "pg_catalog"; admin = NpgsqlDataSource.Create(config.ConnectionString);
        config.SearchPath = name; Source = NpgsqlDataSource.Create(config.ConnectionString);
    }
    public IGameRepository Repository() => new PostgresGameRepository(Source, NullLogger<PostgresGameRepository>.Instance);
    public IItemRepository Items() => new PostgresItemRepository(Source, NullLogger<PostgresItemRepository>.Instance);
    public async Task<long> WaitingLocks()
    {
        await using var command = Source.CreateCommand("SELECT count(*) FROM pg_stat_activity WHERE application_name = @app AND wait_event_type = 'Lock'");
        command.Parameters.AddWithValue("app", name);
        return (long)(await command.ExecuteScalarAsync())!;
    }
    public async Task<long> Scalar(string sql, Guid? id = null)
    {
        await using var command = Source.CreateCommand(sql);
        if (id.HasValue) command.Parameters.AddWithValue("id", id.Value);
        return (long)(await command.ExecuteScalarAsync())!;
    }
    public async Task Execute(string sql) { await using var command = Source.CreateCommand(sql); await command.ExecuteNonQueryAsync(); }
    public static async Task<PrivateSchema> Create(string connection)
    {
        var schema = new PrivateSchema(connection);
        try
        {
            await using var create = schema.admin.CreateCommand($"CREATE SCHEMA \"{schema.name}\"");
            await create.ExecuteNonQueryAsync(); schema.created = true;
            await PostgresSchemaInitializer.ApplyAsync(schema.Source, await PostgresSchemaMigrations.LoadInitializationSqlAsync(AppContext.BaseDirectory, default), default);
            Console.WriteLine("Private fixture: " + schema.name);
            return schema;
        }
        catch { await schema.DisposeAsync(); throw; }
    }
    public async ValueTask DisposeAsync()
    {
        await Source.DisposeAsync();
        try
        {
            if (!created) return;
            Checks.Require(Regex.IsMatch(name, "^rpg_pickup_intent_test_[a-f0-9]{32}$"), "Unsafe private schema cleanup.");
            await using var owner = admin.CreateCommand("SELECT nspowner = (SELECT oid FROM pg_roles WHERE rolname = current_user) FROM pg_namespace WHERE nspname = @name");
            owner.Parameters.AddWithValue("name", name);
            Checks.Require(await owner.ExecuteScalarAsync() is true, "Private schema ownership changed.");
            await using var remove = admin.CreateCommand($"DROP SCHEMA \"{name}\" CASCADE");
            await remove.ExecuteNonQueryAsync(); created = false;
            Console.WriteLine("Private fixture cleaned; existing public data retained.");
        }
        finally { await admin.DisposeAsync(); }
    }
}
