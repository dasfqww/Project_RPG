using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

return await RunAsync(args);

static async Task<int> RunAsync(string[] arguments)
{
    try
    {
        if (arguments.SequenceEqual(["--worker"]))
        {
            NpgsqlConnectionStringBuilder workerConfiguration = ReadConfiguration();
            Require(Regex.IsMatch(workerConfiguration.SearchPath ?? "",
                "^rpg_schema_test_[a-f0-9]{32}$"), "Worker must use a generated test schema.");
            using CancellationTokenSource workerDeadline = new(TimeSpan.FromSeconds(40));
            await using NpgsqlDataSource workerSource =
                NpgsqlDataSource.Create(workerConfiguration.ConnectionString);
            Console.WriteLine("WORKER_READY");
            Require(await Console.In.ReadLineAsync(workerDeadline.Token) == "GO",
                "Worker did not receive its start barrier.");
            await Repository(workerSource).EnsureCreatedAsync(workerDeadline.Token);
            Console.WriteLine("WORKER_SCHEMA_READY");
            return 0;
        }
        Require(arguments.Length == 0, "Unknown schema test arguments.");
        await RunSuiteAsync();
        return 0;
    }
    catch (Exception exception)
    {
        // Never print a connection string, environment, or database credentials.
        string detail = exception is TestFailureException ? exception.Message
            : exception is PostgresException postgres ? $"SQLSTATE {postgres.SqlState}"
            : exception.GetType().Name;
        Console.Error.WriteLine($"FAIL Schema: {detail}");
        return 1;
    }
}

static NpgsqlConnectionStringBuilder ReadConfiguration()
{
    NpgsqlConnectionStringBuilder configuration = new(
        Environment.GetEnvironmentVariable("PROJECT_RPG_SCHEMA_TEST_POSTGRES")
        ?? "Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only");
    Require(configuration.Host is "127.0.0.1" or "localhost" or "::1",
        "Schema tests require a loopback development PostgreSQL instance.");
    // Tests use private schemas; never inherit a public/custom search path.
    configuration.Timeout = 5;
    configuration.CommandTimeout = 10;
    configuration.MaxPoolSize = 16;
    configuration.MinPoolSize = 0;
    configuration.Multiplexing = false;
    configuration.NoResetOnClose = false;
    return configuration;
}

static PostgresGameRepository Repository(NpgsqlDataSource source) =>
    new(source, NullLogger<PostgresGameRepository>.Instance);

static async Task RunSuiteAsync()
{
    NpgsqlConnectionStringBuilder configuration = ReadConfiguration();
    await using SchemaFixture fixture = await SchemaFixture.CreateAsync(configuration);
    string schema = await PostgresSchemaMigrations.LoadInitializationSqlAsync(AppContext.BaseDirectory, default);
    int passed = 0;
    async Task Test(string label, Func<Task> body)
    {
        await body();
        Console.WriteLine($"PASS Schema: {label}");
        passed++;
    }

    await Test("four separate processes initialize a fresh schema", async () =>
    {
        await RunWorkersAsync(fixture.Configuration, 4);
        string[] expectedTables = Regex.Matches(schema,
            @"(?m)^CREATE TABLE IF NOT EXISTS (\w+)")
            .Select(match => match.Groups[1].Value).ToArray();
        Require(expectedTables.Length > 0, "Schema table inventory is empty.");
        foreach (string table in expectedTables)
            Require(await TableExistsAsync(fixture.Source, table), "An expected table was not created.");
    });

    await Test("four actual web hosts start against one fresh schema and become ready", async () =>
    {
        await using SchemaFixture httpFixture = await SchemaFixture.CreateAsync(configuration);
        await RunWebHostsAsync(httpFixture.Configuration, 4);
        Require(await TableExistsAsync(httpFixture.Source, "dungeon_reward_settlements"),
            "Ready web hosts did not apply the production schema.");
        await VerifyLockReleasedAsync(httpFixture);
    });

    DateTimeOffset now = DateTimeOffset.UtcNow;
    string steamId = "76561198000000001"; // isolated schema; no shared player records
    await Repository(fixture.Source).UpsertAccountAsync(
        new SteamIdentity(steamId, steamId, false, false), now, default);
    GameCharacter sentinel = await Repository(fixture.Source).CreateCharacterAsync(
        steamId, "SchemaSentinel", now, default);
    string sentinelTokenHash = new('a', 64); // synthetic, never used as a bearer credential
    await Repository(fixture.Source).StoreSessionAsync(sentinelTokenHash, steamId,
        now.AddHours(1), default);

    async Task VerifySentinel()
    {
        IReadOnlyList<GameCharacter> characters =
            await Repository(fixture.Source).GetCharactersAsync(steamId, default);
        Require(characters.Count == 1 && characters[0].CharacterId == sentinel.CharacterId
            && characters[0].RosterId == sentinel.RosterId && characters[0].SteamId == sentinel.SteamId
            && characters[0].Name == sentinel.Name,
            "Repeated initialization changed the sentinel character.");
        Require(await Repository(fixture.Source).ResolveSessionAsync(
            sentinelTokenHash, now, default) == steamId,
            "Repeated initialization invalidated a current authentication session.");
    }

    await Test("eight independent pools repeatedly initialize an existing schema", async () =>
    {
        for (int round = 0; round < 3; round++)
        {
            TaskCompletionSource barrier = new(TaskCreationOptions.RunContinuationsAsynchronously);
            Task[] jobs = Enumerable.Range(0, 8).Select(async _ =>
            {
                await using NpgsqlDataSource source =
                    NpgsqlDataSource.Create(fixture.Configuration.ConnectionString);
                await barrier.Task;
                await Repository(source).EnsureCreatedAsync(default);
            }).ToArray();
            barrier.SetResult();
            await Task.WhenAll(jobs).WaitAsync(TimeSpan.FromSeconds(30));
        }
        await VerifySentinel();
    });

    await Test("four restarted processes preserve account, character and auth data", async () =>
    {
        await RunWorkersAsync(fixture.Configuration, 4);
        await VerifySentinel();
        Require(await ScalarAsync<long>(fixture.Source,
            "SELECT COUNT(*) FROM rosters;") == 1, "Roster backfill duplicated a roster.");
    });

    await Test("initializer waits for its lock before executing any DDL", async () =>
    {
        await WithHeldLockAsync(fixture, async () =>
        {
            using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(15));
            Task initializer = PostgresSchemaInitializer.ApplyAsync(fixture.Source,
                "CREATE TABLE serialization_probe (value INTEGER);", deadline.Token);
            try
            {
                await WaitForAsync(() => HasWaitingLockAsync(fixture), initializer);
                Require(!initializer.IsCompleted, "Initializer ignored the held schema lock.");
                Require(!await TableExistsAsync(fixture.Source, "serialization_probe"),
                    "DDL ran before the schema lock was acquired.");
            }
            finally
            {
                // Cancel the pending call while the test still owns the blocker.
                deadline.Cancel();
                await ExpectCancellationAsync(initializer);
            }
        });
        await PostgresSchemaInitializer.ApplyAsync(fixture.Source,
            "CREATE TABLE serialization_probe (value INTEGER);", default);
        Require(await TableExistsAsync(fixture.Source, "serialization_probe"),
            "Initializer did not recover after its blocker was released.");
        await VerifyLockReleasedAsync(fixture);
    });

    await Test("cancelled lock wait leaves a single-connection pool reusable", async () =>
    {
        NpgsqlConnectionStringBuilder single = new(fixture.Configuration.ConnectionString)
        {
            MaxPoolSize = 1,
            ApplicationName = fixture.ApplicationName + "_cancel"
        };
        await using NpgsqlDataSource source = NpgsqlDataSource.Create(single.ConnectionString);
        await WithHeldLockAsync(fixture, async () =>
        {
            using CancellationTokenSource cancellation = new(TimeSpan.FromSeconds(15));
            Task initializer = PostgresSchemaInitializer.ApplyAsync(source,
                "CREATE TABLE cancelled_wait_probe (value INTEGER);", cancellation.Token);
            try
            {
                await WaitForAsync(() => HasWaitingLockAsync(fixture, single.ApplicationName), initializer);
            }
            finally
            {
                cancellation.Cancel();
                await ExpectCancellationAsync(initializer);
            }
        });
        Require(!await TableExistsAsync(fixture.Source, "cancelled_wait_probe"),
            "Cancelled wait executed DDL.");
        await Repository(source).EnsureCreatedAsync(default);
        await VerifyLockReleasedAsync(fixture);
    });

    await Test("SQL failure rolls back DDL and releases the schema lock", async () =>
    {
        bool rejected = false;
        try
        {
            await PostgresSchemaInitializer.ApplyAsync(fixture.Source,
                "CREATE TABLE failed_probe (value INTEGER); SELECT 1 / 0;", default);
        }
        catch (PostgresException exception) when (exception.SqlState == PostgresErrorCodes.DivisionByZero)
        {
            rejected = true;
        }
        Require(rejected, "Invalid schema SQL was accepted.");
        Require(!await TableExistsAsync(fixture.Source, "failed_probe"),
            "Failed initialization committed partial DDL.");
        await VerifyLockReleasedAsync(fixture);
        await Repository(fixture.Source).EnsureCreatedAsync(default);
        await VerifySentinel();
    });

    await Test("cancellation during schema execution rolls back and permits retry", async () =>
    {
        NpgsqlConnectionStringBuilder running = new(fixture.Configuration.ConnectionString)
        {
            MaxPoolSize = 1,
            ApplicationName = fixture.ApplicationName + "_running"
        };
        await using NpgsqlDataSource source = NpgsqlDataSource.Create(running.ConnectionString);
        using CancellationTokenSource cancellation = new(TimeSpan.FromSeconds(15));
        Task initializer = PostgresSchemaInitializer.ApplyAsync(source,
            "CREATE TABLE cancelled_ddl_probe (value INTEGER); SELECT pg_sleep(30);", cancellation.Token);
        try
        {
            await WaitForAsync(async () => await ScalarAsync<bool>(fixture.Source,
                "SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE application_name = @name AND wait_event = 'PgSleep');",
                ("name", running.ApplicationName)), initializer);
        }
        finally
        {
            cancellation.Cancel();
            await ExpectCancellationAsync(initializer);
        }
        Require(!await TableExistsAsync(fixture.Source, "cancelled_ddl_probe"),
            "Cancelled initialization committed partial DDL.");
        await Repository(source).EnsureCreatedAsync(default);
        await VerifyLockReleasedAsync(fixture);
    });

    await Test("command timeout bounds lock waits and permits retry", async () =>
    {
        NpgsqlConnectionStringBuilder timed = new(fixture.Configuration.ConnectionString)
        {
            MaxPoolSize = 1,
            CommandTimeout = 1,
            ApplicationName = fixture.ApplicationName + "_timeout"
        };
        await using NpgsqlDataSource source = NpgsqlDataSource.Create(timed.ConnectionString);
        await WithHeldLockAsync(fixture, async () =>
        {
            Stopwatch watch = Stopwatch.StartNew();
            Task initializer = PostgresSchemaInitializer.ApplyAsync(source,
                "CREATE TABLE timed_wait_probe (value INTEGER);", default);
            await WaitForAsync(() => HasWaitingLockAsync(fixture, timed.ApplicationName), initializer);
            bool timedOut = false;
            try { await initializer.WaitAsync(TimeSpan.FromSeconds(8)); }
            catch (NpgsqlException exception) when (exception.InnerException is TimeoutException)
            {
                timedOut = true;
            }
            Require(timedOut && watch.Elapsed < TimeSpan.FromSeconds(8),
                "Schema lock wait did not honor the driver command timeout.");
        });
        Require(!await TableExistsAsync(fixture.Source, "timed_wait_probe"),
            "Timed-out wait executed DDL.");
        await Repository(source).EnsureCreatedAsync(default);
        await VerifyLockReleasedAsync(fixture);
    });

    await Test("existing dungeon member gains a zeroed admission-confirmation generation", async () =>
    {
        await using SchemaFixture upgrade = await SchemaFixture.CreateAsync(configuration);
        IGameRepository repository = Repository(upgrade.Source);
        await repository.EnsureCreatedAsync(default);
        DateTimeOffset created = DateTimeOffset.UtcNow;
        const string memberSteamId = "76561198000000003";
        await repository.UpsertAccountAsync(new SteamIdentity(memberSteamId, memberSteamId, false, false),
            created, default);
        GameCharacter character = await repository.CreateCharacterAsync(memberSteamId, "AdmissionUpgrade", created, default);
        DungeonSession session = await repository.CreateDungeonSessionAsync(memberSteamId, character.CharacterId,
            "Dungeon.Test", "Normal", created, created.AddHours(1), default);
        await ExecuteAsync(upgrade.Source,
            "ALTER TABLE dungeon_session_members DROP COLUMN confirmed_connection_generation;");
        await RunWorkersAsync(upgrade.Configuration, 4);
        Require(await ScalarAsync<long>(upgrade.Source, """
            SELECT confirmed_connection_generation FROM dungeon_session_members
            WHERE character_id = @character_id;
            """, ("character_id", character.CharacterId)) == 0,
            "Existing dungeon member was lost or given a nonzero admission confirmation.");
        Require((await repository.GetDungeonSessionAsync(session.DungeonSessionId, created, default))?.Members.Count == 1,
            "Existing dungeon membership changed during the column upgrade.");
    });

    await Test("existing dungeon member gains a zeroed disconnected generation", async () =>
    {
        await using SchemaFixture upgrade = await SchemaFixture.CreateAsync(configuration);
        IGameRepository repository = Repository(upgrade.Source);
        await repository.EnsureCreatedAsync(default);
        DateTimeOffset created = DateTimeOffset.UtcNow;
        const string memberSteamId = "76561198000000003";
        await repository.UpsertAccountAsync(new SteamIdentity(memberSteamId, memberSteamId, false, false),
            created, default);
        GameCharacter character = await repository.CreateCharacterAsync(memberSteamId,
            "DisconnectUpgrade", created, default);
        DungeonSession session = await repository.CreateDungeonSessionAsync(memberSteamId,
            character.CharacterId, "Dungeon.Test", "Normal", created, created.AddHours(1), default);
        await ExecuteAsync(upgrade.Source,
            "ALTER TABLE dungeon_session_members DROP COLUMN disconnected_generation;");
        await RunWorkersAsync(upgrade.Configuration, 4);
        Require(await ScalarAsync<long>(upgrade.Source, """
            SELECT disconnected_generation FROM dungeon_session_members
            WHERE character_id = @character_id;
            """, ("character_id", character.CharacterId)) == 0,
            "Existing dungeon member was lost or assigned a disconnect tombstone during upgrade.");
        Require((await repository.GetDungeonSessionAsync(session.DungeonSessionId, created, default))?
                .Members.Single().DisconnectedGeneration == 0,
            "Upgraded disconnect generation did not read as zero.");
    });

    await Test("four processes upgrade a legacy schema without losing its character", async () =>
    {
        await using SchemaFixture legacy = await SchemaFixture.CreateAsync(configuration);
        // Minimal earlier schema: no rosters and no character roster_id column.
        await ExecuteAsync(legacy.Source, """
            CREATE TABLE accounts (
                steam_id VARCHAR(20) PRIMARY KEY, owner_steam_id VARCHAR(20) NOT NULL,
                vac_banned BOOLEAN NOT NULL DEFAULT FALSE, publisher_banned BOOLEAN NOT NULL DEFAULT FALSE,
                created_at TIMESTAMPTZ NOT NULL, last_login_at TIMESTAMPTZ NOT NULL);
            CREATE TABLE characters (
                character_id UUID PRIMARY KEY,
                steam_id VARCHAR(20) NOT NULL REFERENCES accounts(steam_id) ON DELETE CASCADE,
                name VARCHAR(20) NOT NULL, created_at TIMESTAMPTZ NOT NULL, updated_at TIMESTAMPTZ NOT NULL);
            INSERT INTO accounts VALUES ('76561198000000002', '76561198000000002', FALSE, FALSE, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP);
            INSERT INTO characters VALUES ('00000000-0000-0000-0000-000000000001', '76561198000000002', 'LegacySentinel', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP);
            """);
        await RunWorkersAsync(legacy.Configuration, 4);
        Require(await ScalarAsync<long>(legacy.Source, """
            SELECT COUNT(*) FROM characters AS c
            JOIN rosters AS r ON c.roster_id = r.roster_id AND c.steam_id = r.steam_id
            WHERE c.character_id = '00000000-0000-0000-0000-000000000001'
              AND c.name = 'LegacySentinel' AND r.world_id = 'main';
            """) == 1, "Legacy character or roster backfill was lost/duplicated.");
        await VerifyLockReleasedAsync(legacy);
    });

    Console.WriteLine($"PASS Schema suite: {passed} tests; private schemas are cleaned up on disposal.");
}

static async Task RunWorkersAsync(NpgsqlConnectionStringBuilder configuration, int count)
{
    List<Process> workers = [];
    using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(40));
    try
    {
        for (int index = 0; index < count; index++)
        {
            ProcessStartInfo start = new("dotnet")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            start.ArgumentList.Add(Assembly.GetExecutingAssembly().Location);
            start.ArgumentList.Add("--worker");
            start.Environment["PROJECT_RPG_SCHEMA_TEST_POSTGRES"] = configuration.ConnectionString;
            Process worker = new() { StartInfo = start };
            Require(worker.Start(), "Unable to start a schema worker.");
            workers.Add(worker);
        }
        string?[] ready = await Task.WhenAll(workers.Select(worker =>
            worker.StandardOutput.ReadLineAsync(deadline.Token).AsTask()));
        Require(ready.All(line => line == "WORKER_READY"), "A schema worker did not reach its start barrier.");
        foreach (Process worker in workers)
            await worker.StandardInput.WriteLineAsync("GO");
        await Task.WhenAll(workers.Select(worker => worker.WaitForExitAsync(deadline.Token)));
        foreach (Process worker in workers)
        {
            string output = await worker.StandardOutput.ReadToEndAsync(deadline.Token);
            string error = await worker.StandardError.ReadToEndAsync(deadline.Token);
            Require(worker.ExitCode == 0 && output.Contains("WORKER_SCHEMA_READY", StringComparison.Ordinal),
                $"A schema worker failed (exit {worker.ExitCode}); {error.Trim()}");
        }
    }
    finally
    {
        foreach (Process worker in workers)
        {
            if (!worker.HasExited)
            {
                worker.Kill(entireProcessTree: true); // only processes created above
                await worker.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
            }
            worker.Dispose();
        }
    }
}

static async Task RunWebHostsAsync(NpgsqlConnectionStringBuilder configuration, int count)
{
    List<Process> hosts = [];
    List<Task<Uri>> addresses = [];
    List<Task<string>> errors = [];
    using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(40));
    try
    {
        for (int index = 0; index < count; index++)
        {
            ProcessStartInfo start = new("dotnet")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden,
                WorkingDirectory = AppContext.BaseDirectory,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            start.ArgumentList.Add(typeof(PostgresGameRepository).Assembly.Location);
            start.Environment["ASPNETCORE_ENVIRONMENT"] = "Testing";
            start.Environment["POSTGRES_CONNECTION_STRING"] = configuration.ConnectionString;
            start.Environment["PROJECT_RPG_BACKEND_ADMIN_TOKEN"] =
                Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
            // Let the OS choose distinct unused loopback ports; no bind race.
            start.Environment["Kestrel__Endpoints__Http__Url"] = "http://127.0.0.1:0";
            start.Environment["Steam__AllowDevelopmentAuthentication"] = "false";
            start.Environment["Logging__LogLevel__Default"] = "Warning";
            start.Environment["Logging__LogLevel__Microsoft.Hosting.Lifetime"] = "Information";
            Process host = new() { StartInfo = start };
            Require(host.Start(), "Unable to start a backend web host.");
            hosts.Add(host);
            addresses.Add(ReadListeningAddressAsync(host, deadline.Token));
            errors.Add(host.StandardError.ReadToEndAsync(deadline.Token));
        }
        Uri[] endpoints = await Task.WhenAll(addresses);
        Require(endpoints.Select(endpoint => endpoint.Port).Distinct().Count() == count,
            "Backend web hosts did not bind distinct ports.");
        using HttpClient client = new() { Timeout = TimeSpan.FromSeconds(5) };
        foreach (Uri endpoint in endpoints)
        {
            using HttpResponseMessage live = await client.GetAsync(new Uri(endpoint, "/health/live"), deadline.Token);
            Require(live.IsSuccessStatusCode, "Concurrent web host failed liveness.");
            using HttpResponseMessage ready = await client.GetAsync(new Uri(endpoint, "/health/ready"), deadline.Token);
            Require(ready.IsSuccessStatusCode, "Concurrent web host failed readiness.");
            using JsonDocument json = JsonDocument.Parse(await ready.Content.ReadAsStringAsync(deadline.Token));
            Require(json.RootElement.GetProperty("status").GetString() == "ready"
                && json.RootElement.GetProperty("storageProvider").GetString() == "Postgres",
                "Concurrent web host readiness did not check PostgreSQL.");
        }
        Require(hosts.All(host => !host.HasExited), "A concurrent web host exited unexpectedly.");
    }
    finally
    {
        foreach (Process host in hosts)
        {
            if (!host.HasExited)
            {
                host.Kill(entireProcessTree: true); // only hosts created above
                await host.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
            }
            host.Dispose();
        }
        // Observe pumps even when startup fails; do not leave abandoned reads.
        deadline.Cancel();
        foreach (Task task in addresses.Cast<Task>().Concat(errors))
        {
            try { await task; }
            catch (Exception) { /* startup assertion above owns failure reporting */ }
        }
    }
}

static async Task<Uri> ReadListeningAddressAsync(Process host, CancellationToken cancellationToken)
{
    while (await host.StandardOutput.ReadLineAsync(cancellationToken) is { } line)
    {
        Match match = Regex.Match(line, @"Now listening on: (http://127\.0\.0\.1:\d+)");
        if (match.Success) return new Uri(match.Groups[1].Value);
    }
    throw new TestFailureException("Backend exited without binding a loopback HTTP endpoint.");
}

static async Task WithHeldLockAsync(SchemaFixture fixture, Func<Task> body)
{
    await using NpgsqlConnection connection = await fixture.Source.OpenConnectionAsync();
    await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync();
    await using NpgsqlCommand command = new(
        "SELECT pg_advisory_xact_lock(@namespace, @resource);", connection, transaction);
    command.Parameters.AddWithValue("namespace", PostgresSchemaInitializer.LockNamespace);
    command.Parameters.AddWithValue("resource", PostgresSchemaInitializer.LockResource);
    await command.ExecuteNonQueryAsync();
    try { await body(); }
    finally { await transaction.RollbackAsync(); }
}

static Task<bool> HasWaitingLockAsync(SchemaFixture fixture, string? applicationName = null) =>
    ScalarAsync<bool>(fixture.Source, """
        SELECT EXISTS (
            SELECT 1 FROM pg_locks AS l JOIN pg_stat_activity AS a ON a.pid = l.pid
            WHERE l.locktype = 'advisory' AND NOT l.granted
              AND l.classid::bigint = @namespace AND l.objid::bigint = @resource AND l.objsubid = 2
              AND l.database = (SELECT oid FROM pg_database WHERE datname = current_database())
              AND a.application_name = @name);
        """, ("namespace", (long)PostgresSchemaInitializer.LockNamespace),
        ("resource", (long)PostgresSchemaInitializer.LockResource),
        ("name", applicationName ?? fixture.ApplicationName));

static async Task VerifyLockReleasedAsync(SchemaFixture fixture)
{
    // A fresh physical connection cannot mistake a leaked, reentrant lock on a
    // returned pooled connection for a successful release.
    NpgsqlConnectionStringBuilder observer = new(fixture.Configuration.ConnectionString) { Pooling = false };
    await using NpgsqlDataSource source = NpgsqlDataSource.Create(observer.ConnectionString);
    await using NpgsqlConnection connection = await source.OpenConnectionAsync();
    await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync();
    await using NpgsqlCommand command = new(
        "SELECT pg_try_advisory_xact_lock(@namespace, @resource);", connection, transaction);
    command.Parameters.AddWithValue("namespace", PostgresSchemaInitializer.LockNamespace);
    command.Parameters.AddWithValue("resource", PostgresSchemaInitializer.LockResource);
    Require(await command.ExecuteScalarAsync() is true, "Schema lock leaked after initialization.");
    await transaction.RollbackAsync();
}

static async Task WaitForAsync(Func<Task<bool>> condition, Task operation)
{
    Stopwatch watch = Stopwatch.StartNew();
    while (watch.Elapsed < TimeSpan.FromSeconds(8))
    {
        if (await condition()) return;
        Require(!operation.IsCompleted, "Initializer completed before the expected database wait was observed.");
        await Task.Delay(25);
    }
    throw new TestFailureException("Expected database wait was not observed.");
}

static async Task ExpectCancellationAsync(Task operation)
{
    bool cancelled = false;
    try { await operation.WaitAsync(TimeSpan.FromSeconds(8)); }
    catch (OperationCanceledException) { cancelled = true; }
    Require(cancelled, "Initialization did not honor caller cancellation.");
}

static Task<bool> TableExistsAsync(NpgsqlDataSource source, string table) =>
    ScalarAsync<bool>(source, "SELECT to_regclass(@table) IS NOT NULL;", ("table", table));

static async Task<T> ScalarAsync<T>(NpgsqlDataSource source, string sql,
    params (string Name, object Value)[] parameters)
{
    await using NpgsqlCommand command = source.CreateCommand(sql);
    foreach (var parameter in parameters)
        command.Parameters.AddWithValue(parameter.Name, parameter.Value);
    object? result = await command.ExecuteScalarAsync();
    Require(result is T, "Unexpected SQL scalar result type.");
    return (T)result!;
}

static async Task ExecuteAsync(NpgsqlDataSource source, string sql)
{
    await using NpgsqlCommand command = source.CreateCommand(sql);
    await command.ExecuteNonQueryAsync();
}

static void Require(bool condition, string message)
{
    if (!condition) throw new TestFailureException(message);
}

sealed class TestFailureException(string message) : Exception(message);

sealed class SchemaFixture : IAsyncDisposable
{
    private readonly NpgsqlDataSource adminSource;
    private bool created;
    public string Name { get; } = "rpg_schema_test_" + Guid.NewGuid().ToString("N");
    public string ApplicationName => "SchemaTests_" + Name[16..];
    public NpgsqlConnectionStringBuilder Configuration { get; }
    public NpgsqlDataSource Source { get; }

    private SchemaFixture(NpgsqlConnectionStringBuilder configuration)
    {
        NpgsqlConnectionStringBuilder admin = new(configuration.ConnectionString)
        {
            SearchPath = "pg_catalog",
            ApplicationName = ApplicationName + "_admin"
        };
        adminSource = NpgsqlDataSource.Create(admin.ConnectionString);
        Configuration = new(configuration.ConnectionString)
        {
            SearchPath = Name,
            ApplicationName = ApplicationName
        };
        Source = NpgsqlDataSource.Create(Configuration.ConnectionString);
    }

    public static async Task<SchemaFixture> CreateAsync(NpgsqlConnectionStringBuilder configuration)
    {
        SchemaFixture fixture = new(configuration);
        try
        {
            await using NpgsqlCommand command = fixture.adminSource.CreateCommand(
                $"CREATE SCHEMA \"{fixture.Name}\";");
            await command.ExecuteNonQueryAsync();
            fixture.created = true;
            Console.WriteLine($"Fixture: {fixture.Name} (private schema; public data untouched)");
            return fixture;
        }
        catch
        {
            await fixture.DisposeAsync();
            throw;
        }
    }

    public async ValueTask DisposeAsync()
    {
        await Source.DisposeAsync();
        try
        {
            if (!created) return;
            // Exact, generated identifier; never accept a configured name or
            // DROP a database/public schema. Verify ownership before cleanup.
            if (!Regex.IsMatch(Name, "^rpg_schema_test_[a-f0-9]{32}$"))
                throw new TestFailureException("Unsafe fixture cleanup target.");
            await using NpgsqlCommand owner = adminSource.CreateCommand("""
                SELECT nspowner = (SELECT oid FROM pg_roles WHERE rolname = current_user)
                FROM pg_namespace WHERE nspname = @name;
                """);
            owner.Parameters.AddWithValue("name", Name);
            if (await owner.ExecuteScalarAsync() is not true)
                throw new TestFailureException("Fixture schema ownership changed; cleanup refused.");
            await using NpgsqlCommand cleanup = adminSource.CreateCommand($"DROP SCHEMA \"{Name}\" CASCADE;");
            await cleanup.ExecuteNonQueryAsync();
            created = false;
            Console.WriteLine($"Fixture cleaned: {Name}");
        }
        finally { await adminSource.DisposeAsync(); }
    }
}
