using System.Diagnostics;
using System.Text.RegularExpressions;
using Npgsql;
using ProjectRpg.Backend.Data;

try
{
    Require(args.Length == 0 || args.SequenceEqual(["--postgres"]) || args.SequenceEqual(["--postgres-only"]),
        "Unknown migration test arguments.");
    if (!args.SequenceEqual(["--postgres-only"])) await RunPureAsync();
    if (args.Length != 0) await RunPostgresAsync();
    else Console.WriteLine("NOT RUN Migration: PostgreSQL integration (explicit --postgres required).");
    return 0;
}
catch (Exception exception)
{
    // Do not log SQL, connection strings, credentials or child environment.
    Console.Error.WriteLine("FAIL Migration: " + (exception is TestFailure ? exception.Message
        : exception is PostgresException pg ? "SQLSTATE " + pg.SqlState : exception.GetType().Name));
    return 1;
}

static async Task RunPureAsync()
{
    PostgresSchemaMigration first = new(1, "baseline", "CREATE TABLE sample (id INTEGER);\r\n");
    PostgresSchemaMigration second = new(2, "next_version", "ALTER TABLE sample ADD COLUMN label TEXT;");
    PostgresSchemaMigration[] catalog = [first, second];
    PostgresSchemaMigrationHistory Applied(PostgresSchemaMigration migration) =>
        new(migration.Version, migration.Name, migration.Sha256);
    Check("new catalog and applied prefix select only pending versions", () =>
    {
        Require(PostgresSchemaMigrations.GetPending(catalog, []).SequenceEqual(catalog), "Fresh plan changed order.");
        Require(PostgresSchemaMigrations.GetPending(catalog, [Applied(first)]).SequenceEqual([second]), "Prefix was reapplied.");
        Require(PostgresSchemaMigrations.GetPending(catalog, [Applied(first), Applied(second)]).Count == 0, "Current catalog was reapplied.");
    });
    Check("line endings normalize but SQL and names remain immutable", () =>
    {
        Require(first.Sha256 == new PostgresSchemaMigration(1, "baseline", first.Sql.Replace("\n", "\r\n")).Sha256,
            "Line endings changed identity.");
        Require(first.Sha256 != new PostgresSchemaMigration(1, "baseline", first.Sql + "-- edit").Sha256,
            "SQL edit was not detected.");
        Reject(() => PostgresSchemaMigrations.GetPending(catalog, [new(1, "renamed", first.Sha256)]));
        Reject(() => PostgresSchemaMigrations.GetPending(catalog, [new(1, "baseline", new string('0', 64))]));
    });
    Check("empty gaps duplicate names and null catalog entries fail before SQL", () =>
    {
        Reject(() => PostgresSchemaMigrations.GetPending([], []));
        Reject(() => PostgresSchemaMigrations.GetPending([second], []));
        Reject(() => PostgresSchemaMigrations.GetPending([first, new(3, "third", "SELECT 1;")], []));
        Reject(() => PostgresSchemaMigrations.GetPending([first, new(2, "baseline", "SELECT 1;")], []));
        Reject(() => PostgresSchemaMigrations.GetPending([first, null!], []));
        Reject(() => new PostgresSchemaMigration(0, "bad", "SELECT 1;"));
        Reject(() => new PostgresSchemaMigration(1, "bad name", "SELECT 1;"));
        Reject(() => new PostgresSchemaMigration(1, "bad\n", "SELECT 1;"));
        Reject(() => new PostgresSchemaMigration(1, "empty", " "));
    });
    Check("future missing reordered duplicate and null history fail closed", () =>
    {
        Reject(() => PostgresSchemaMigrations.GetPending([first], [Applied(first), Applied(second)]));
        Reject(() => PostgresSchemaMigrations.GetPending(catalog, [Applied(second)]));
        Reject(() => PostgresSchemaMigrations.GetPending(catalog, [Applied(second), Applied(first)]));
        Reject(() => PostgresSchemaMigrations.GetPending(catalog, [Applied(first), Applied(first)]));
        Reject(() => PostgresSchemaMigrations.GetPending(catalog, [null!]));
    });
    IReadOnlyList<PostgresSchemaMigration> packaged =
        await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
    Check("packaged baseline and equipment delta are present", () =>
    {
        Require(packaged.Count == 13 && packaged[0].Version == 1 && packaged[0].Name == "baseline"
            && packaged[0].Sql.Contains("CREATE TABLE IF NOT EXISTS accounts", StringComparison.Ordinal)
            && packaged[1].Version == 2 && packaged[1].Name == "item_equipment_progression"
            && packaged[1].Sql.Contains("equipment_progression", StringComparison.Ordinal)
            && packaged[2].Version == 3 && packaged[2].Name == "character_narrative"
            && packaged[2].Sql.Contains("character_narrative_commands", StringComparison.Ordinal)
            && packaged[3].Version == 4 && packaged[3].Name == "operations_sessions"
            && packaged[3].Sql.Contains("ops_operator_sessions", StringComparison.Ordinal)
            && packaged[4].Version == 5 && packaged[4].Name == "operations_evidence"
            && packaged[4].Sql.Contains("ix_ops_item_receipt_result_owner", StringComparison.Ordinal)
            && packaged[5].Version == 6 && packaged[5].Name == "operations_compensation"
            && packaged[5].Sql.Contains("ops_compensation_cases", StringComparison.Ordinal)
            && packaged[6].Version == 7 && packaged[6].Name == "operations_restrictions"
            && packaged[6].Sql.Contains("ops_account_restrictions", StringComparison.Ordinal)
            && packaged[7].Version == 8 && packaged[7].Name == "trade_reservations"
            && packaged[8].Version == 9 && packaged[8].Name == "operations_service_control"
            && packaged[8].Sql.Contains("ops_service_control", StringComparison.Ordinal)
            && packaged[9].Version == 10 && packaged[9].Name == "trade_sessions"
            && packaged[9].Sql.Contains("trade_session_receipts", StringComparison.Ordinal)
            && packaged[10].Version == 11 && packaged[10].Name == "inventory_tabs"
            && packaged[11].Version == 12 && packaged[11].Name == "auction_escrow"
            && packaged[11].Sql.Contains("auction_escrow_receipts", StringComparison.Ordinal)
            && packaged[12].Version == 13 && packaged[12].Name == "auction_sales"
            && packaged[12].Sql.Contains("auction_seller_payables", StringComparison.Ordinal),
            "Packaged baseline or equipment delta is missing.");
    });
    foreach (string environment in new[] { "Development", "Production" })
    {
        (int exitCode, string output) = await RunBackendAsync(environment, null, "--migrate-schema");
        Require(exitCode != 0 && output.Contains("--migrate-schema requires PostgreSQL storage.", StringComparison.Ordinal)
            && !output.Contains("Now listening", StringComparison.Ordinal), "Migration command started a memory host.");
    }
    Console.WriteLine("PASS Migration: actual deployment command rejects Memory before hosting (Development/Production)");
    (int duplicateExit, string duplicateOutput) = await RunBackendAsync("Production", null,
        "--migrate-schema", "--migrate-schema");
    Require(duplicateExit != 0 && duplicateOutput.Contains("Specify --migrate-schema only once.", StringComparison.Ordinal),
        "Duplicate migration command was accepted.");
    Console.WriteLine("PASS Migration: duplicate command is rejected before host construction");
}

static async Task RunPostgresAsync()
{
    NpgsqlConnectionStringBuilder configuration = new(Environment.GetEnvironmentVariable("PROJECT_RPG_SCHEMA_TEST_POSTGRES")
        ?? "Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only");
    Require(configuration.Host is "127.0.0.1" or "localhost" or "::1", "Integration requires loopback PostgreSQL.");
    configuration.Timeout = 5;
    configuration.CommandTimeout = 10;
    configuration.Multiplexing = false;
    await using MigrationFixture fixture = await MigrationFixture.CreateAsync(configuration);
    PostgresSchemaMigration first = new(1, "baseline", "CREATE TABLE sentinel (id INTEGER PRIMARY KEY); INSERT INTO sentinel VALUES (7);");
    PostgresSchemaMigration second = new(2, "label", "ALTER TABLE sentinel ADD COLUMN label TEXT;");
    PostgresSchemaMigration[] catalog = [first, second];
    Require(await PostgresSchemaMigrations.ApplyAsync(fixture.Source, [first], default) == 1, "Initial migration not applied.");
    Require(await PostgresSchemaMigrations.ApplyAsync(fixture.Source, [first], default) == 0, "Non-idempotent SQL reapplied.");
    int[] concurrent = await Task.WhenAll(Enumerable.Range(0, 4).Select(_ =>
        PostgresSchemaMigrations.ApplyAsync(fixture.Source, catalog, default)));
    Require(concurrent.Sum() == 1, "Concurrent deployers applied a migration more than once.");
    Require(await ScalarAsync<long>(fixture.Source, "SELECT COUNT(*) FROM sentinel WHERE id = 7;") == 1,
        "Existing data changed.");
    Console.WriteLine("PASS Migration PG: baseline/no-op/parallel delta preserve sentinel data");

    NpgsqlConnectionStringBuilder readOnly = new(fixture.Configuration.ConnectionString)
    {
        Options = "-c default_transaction_read_only=on"
    };
    await using (NpgsqlDataSource reader = NpgsqlDataSource.Create(readOnly.ConnectionString))
        await PostgresSchemaMigrations.ValidateAsync(reader, catalog, default);
    await RejectAsync(() => PostgresSchemaMigrations.ValidateAsync(fixture.Source, [first], default));
    await RejectAsync(() => PostgresSchemaMigrations.ApplyAsync(fixture.Source,
        [first, new(2, "label", second.Sql + "-- changed")], default));
    Console.WriteLine("PASS Migration PG: read-only validation and changed/newer history rejection");

    PostgresSchemaMigration bad = new(3, "fails", "CREATE TABLE rollback_probe (id INTEGER); SELECT missing_rpg_migration_function();");
    await RejectAsync(() => PostgresSchemaMigrations.ApplyAsync(fixture.Source, [first, second, bad], default));
    Require(await ScalarAsync<long>(fixture.Source, "SELECT COUNT(*) FROM rpg_schema_migrations;") == 2,
        "Failed SQL stamped history.");
    Require(!await ScalarAsync<bool>(fixture.Source, "SELECT to_regclass('rollback_probe') IS NOT NULL;"),
        "Failed migration left partial DDL.");
    await PostgresSchemaMigrations.ValidateAsync(fixture.Source, catalog, default);
    Console.WriteLine("PASS Migration PG: failed pending SQL and its history roll back together");

    await using MigrationFixture fresh = await MigrationFixture.CreateAsync(configuration);
    await RejectAsync(() => PostgresSchemaMigrations.ValidateAsync(fresh.Source, [first], default));
    Require(!await ScalarAsync<bool>(fresh.Source, "SELECT to_regclass('rpg_schema_migrations') IS NOT NULL;"),
        "Validation wrote missing history.");
    await RejectAsync(() => PostgresSchemaMigrations.ApplyAsync(fresh.Source,
        [new(1, "fails", bad.Sql)], default));
    Require(!await ScalarAsync<bool>(fresh.Source, "SELECT to_regclass('rpg_schema_migrations') IS NOT NULL;"),
        "Failed initial migration left its history table.");
    Console.WriteLine("PASS Migration PG: missing history is not repaired and initial failure is atomic");

    // Exercise the packaged command against only our generated private schema.
    IReadOnlyList<PostgresSchemaMigration> packaged = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
    (int commandExit, string output) = await RunBackendAsync("Production", fresh.Configuration.ConnectionString, "--migrate-schema");
    Require(commandExit == 0 && output.Contains($"applied {packaged.Count} version(s). No web host started.", StringComparison.Ordinal)
        && !output.Contains("Now listening", StringComparison.Ordinal), "Deployment command did not complete without hosting.");
    await PostgresSchemaMigrations.ValidateAsync(fresh.Source, packaged, default);
    Require(await ScalarAsync<bool>(fresh.Source, "SELECT to_regclass('accounts') IS NOT NULL;"), "Real baseline missing.");
    Console.WriteLine("PASS Migration PG: actual command applies packaged baseline without starting a host");
    if (Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_MIGRATION_TEST_ISOLATED_RUN"), "N", out _))
        await MigrationProductionChecks.RunAsync(configuration, packaged);
    else Console.WriteLine("NOT RUN Migration PG: role/production checks require the owned isolated container runner.");
}

static async Task<(int ExitCode, string Output)> RunBackendAsync(string environment, string? connectionString,
    params string[] arguments)
{
    ProcessStartInfo start = new("dotnet")
    {
        UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
        WorkingDirectory = AppContext.BaseDirectory, RedirectStandardOutput = true, RedirectStandardError = true
    };
    start.ArgumentList.Add(typeof(PostgresSchemaMigrations).Assembly.Location);
    foreach (string argument in arguments) start.ArgumentList.Add(argument);
    start.Environment["ASPNETCORE_ENVIRONMENT"] = environment;
    start.Environment["DOTNET_ENVIRONMENT"] = environment;
    start.Environment.Remove("POSTGRES_CONNECTION_STRING");
    start.Environment.Remove("ConnectionStrings__Postgres");
    start.Environment["Storage__Provider"] = connectionString is null ? "Memory" : "Postgres";
    if (connectionString is not null) start.Environment["POSTGRES_CONNECTION_STRING"] = connectionString;
    using Process child = new() { StartInfo = start };
    Require(child.Start(), "Could not start owned backend command process.");
    Task<string> output = child.StandardOutput.ReadToEndAsync();
    Task<string> error = child.StandardError.ReadToEndAsync();
    try
    {
        await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(35));
        return (child.ExitCode, await output + await error);
    }
    finally
    {
        if (!child.HasExited)
        {
            child.Kill(entireProcessTree: true);
            await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
        }
    }
}

static void Check(string name, Action body) { body(); Console.WriteLine("PASS Migration: " + name); }
static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
static void Reject(Action body)
{
    try { body(); }
    catch (Exception exception) when (exception is ArgumentException or InvalidOperationException) { return; }
    throw new TestFailure("Invalid migration plan was accepted.");
}
static async Task RejectAsync(Func<Task> body)
{
    try { await body(); }
    catch (Exception exception) when (exception is InvalidOperationException or PostgresException) { return; }
    throw new TestFailure("Invalid database migration was accepted.");
}
static async Task<T> ScalarAsync<T>(NpgsqlDataSource source, string sql)
{
    await using NpgsqlCommand command = source.CreateCommand(sql);
    object? value = await command.ExecuteScalarAsync();
    Require(value is T, "Unexpected database scalar type.");
    return (T)value!;
}
sealed class TestFailure(string message) : Exception(message);

sealed class MigrationFixture : IAsyncDisposable
{
    public string Name { get; } = "rpg_migration_test_" + Guid.NewGuid().ToString("N");
    public NpgsqlConnectionStringBuilder Configuration { get; }
    public NpgsqlDataSource Source { get; }
    private readonly NpgsqlDataSource admin;
    private bool created;
    private MigrationFixture(NpgsqlConnectionStringBuilder configuration)
    {
        NpgsqlConnectionStringBuilder adminConfiguration = new(configuration.ConnectionString) { SearchPath = "pg_catalog" };
        admin = NpgsqlDataSource.Create(adminConfiguration.ConnectionString);
        Configuration = new(configuration.ConnectionString) { SearchPath = Name };
        Source = NpgsqlDataSource.Create(Configuration.ConnectionString);
    }
    public static async Task<MigrationFixture> CreateAsync(NpgsqlConnectionStringBuilder configuration)
    {
        MigrationFixture fixture = new(configuration);
        try
        {
            await using NpgsqlCommand command = fixture.admin.CreateCommand($"CREATE SCHEMA \"{fixture.Name}\";");
            await command.ExecuteNonQueryAsync();
            fixture.created = true;
            Console.WriteLine("Fixture created: " + fixture.Name);
            return fixture;
        }
        catch { await fixture.DisposeAsync(); throw; }
    }
    public async ValueTask DisposeAsync()
    {
        await Source.DisposeAsync();
        try
        {
            if (!created) return;
            if (!Regex.IsMatch(Name, "\\Arpg_migration_test_[a-f0-9]{32}\\z"))
                throw new TestFailure("Unsafe fixture cleanup target.");
            await using NpgsqlCommand owner = admin.CreateCommand("""
                SELECT nspowner = (SELECT oid FROM pg_roles WHERE rolname = current_user)
                FROM pg_namespace WHERE nspname = @name;
                """);
            owner.Parameters.AddWithValue("name", Name);
            if (await owner.ExecuteScalarAsync() is not true)
                throw new TestFailure("Fixture owner changed; cleanup refused.");
            await using NpgsqlCommand command = admin.CreateCommand($"DROP SCHEMA \"{Name}\" CASCADE;");
            await command.ExecuteNonQueryAsync();
            created = false;
            Console.WriteLine("Fixture cleaned: " + Name);
        }
        finally { await admin.DisposeAsync(); }
    }
}
