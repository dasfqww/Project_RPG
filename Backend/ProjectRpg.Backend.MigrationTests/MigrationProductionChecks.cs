using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

// Runs only in the runner's uniquely labelled, disposable PostgreSQL container.
// Never provision roles or start production hosts against the shared game DB.
internal static class MigrationProductionChecks
{
    public static async Task RunAsync(NpgsqlConnectionStringBuilder configuration,
        IReadOnlyList<PostgresSchemaMigration> catalog)
    {
        await using MigrationFixture fixture = await MigrationFixture.CreateAsync(configuration);
        string role = "rpg_migration_runtime_" + Guid.NewGuid().ToString("N");
        string password = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
        bool roleCreated = false;
        try
        {
            // Exercise adoption of an existing unversioned schema, not just a new DB.
            await PostgresSchemaInitializer.ApplyAsync(fixture.Source, string.Join("\n", catalog.Select(m => m.Sql)), default);
            PostgresGameRepository repository = new(fixture.Source, NullLogger<PostgresGameRepository>.Instance);
            DateTimeOffset now = DateTimeOffset.UtcNow;
            const string steamId = "76561198000000009";
            string tokenHash = new('c', 64); // synthetic; never used as a bearer token
            await repository.UpsertAccountAsync(new SteamIdentity(steamId, steamId, false, false), now, default);
            GameCharacter character = await repository.CreateCharacterAsync(steamId, "MigrationSentinel", now, default);
            await repository.StoreSessionAsync(tokenHash, steamId, now.AddHours(1), default);
            await using (NpgsqlCommand seed = fixture.Source.CreateCommand("""
                INSERT INTO item_records (item_id, definition_type, definition_name, definition_version,
                    owner_type, owner_id, container_type, container_id, slot_index, generation_seed,
                    quantity, revision, lifecycle_state, bind_state, created_at, updated_at)
                VALUES (@inventory, 'Item', 'MigrationSentinel', 1, 'Character', @character,
                        'Inventory', 'Main', 0, 123, 7, 9007199254740993, 'Active', 'Unbound', @now, @now),
                       (@mail, 'Item', 'MigrationSentinel', 1, 'Character', @character,
                        'Mail', 'MigrationReward', 0, 456, 3, 2, 'Active', 'CharacterBound', @now, @now);
                INSERT INTO currency_definitions (currency_code, display_name, scope, max_balance,
                    enabled, created_at, updated_at)
                VALUES ('MigrationGold', 'MigrationGold', 'Character', 9223372036854775807, TRUE, @now, @now);
                INSERT INTO currency_balances (owner_type, owner_id, currency_code, balance, revision, updated_at)
                VALUES ('Character', @character, 'MigrationGold', 9007199254740993, 7, @now);
                """))
            {
                seed.Parameters.AddWithValue("inventory", Guid.NewGuid());
                seed.Parameters.AddWithValue("mail", Guid.NewGuid());
                seed.Parameters.AddWithValue("character", character.CharacterId.ToString("D"));
                seed.Parameters.AddWithValue("now", now);
                await seed.ExecuteNonQueryAsync();
            }
            string playerBefore = await PlayerSnapshotAsync(fixture.Source);
            await ExecuteAsync(fixture.Source, $"CREATE ROLE \"{role}\" LOGIN PASSWORD '{password}' NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT;");
            roleCreated = true;
            await ExecuteAsync(fixture.Source, $"""
                GRANT USAGE ON SCHEMA "{fixture.Name}" TO "{role}";
                GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA "{fixture.Name}" TO "{role}";
                """);
            NpgsqlConnectionStringBuilder runtime = new(fixture.Configuration.ConnectionString)
            {
                Username = role, Password = password, IncludeErrorDetail = false
            };
            await RunHostAsync(runtime.ConnectionString, "Database migration history is missing.");
            Require(!await ScalarAsync<bool>(fixture.Source, "SELECT to_regclass('rpg_schema_migrations') IS NOT NULL;"),
                "Normal production startup created migration history.");
            Require(await PostgresSchemaMigrations.ApplyAsync(fixture.Source, catalog, default) == catalog.Count,
                "Existing schema was not adopted by executing its baseline.");
            Require(await PlayerSnapshotAsync(fixture.Source) == playerBefore,
                "Baseline adoption changed account/character/auth/inventory/mail/currency rows.");
            Require(await repository.ResolveSessionAsync(tokenHash, now, default) == steamId,
                "Adoption invalidated a valid authentication session.");
            Console.WriteLine("PASS Migration PG: real unversioned player/inventory/mail/currency data survives baseline adoption");

            await ExecuteAsync(fixture.Source, $"GRANT SELECT ON rpg_schema_migrations TO \"{role}\";");
            await using (NpgsqlDataSource source = NpgsqlDataSource.Create(runtime.ConnectionString))
            {
                Require(await ScalarAsync<bool>(source, """
                    SELECT NOT rolsuper AND NOT rolcreatedb AND NOT rolcreaterole
                        AND NOT has_schema_privilege(current_schema(), 'CREATE')
                        AND has_table_privilege('rpg_schema_migrations', 'SELECT')
                        AND NOT has_table_privilege('rpg_schema_migrations', 'INSERT')
                        AND NOT has_table_privilege('rpg_schema_migrations', 'UPDATE')
                        AND NOT has_table_privilege('rpg_schema_migrations', 'DELETE')
                    FROM pg_roles WHERE rolname = current_user;
                    """), "Runtime fixture has elevated/DDL/history-write privileges.");
                await PostgresSchemaMigrations.ValidateAsync(source, catalog, default);
                await ExpectDeniedAsync(() => ExecuteAsync(source, "CREATE TABLE forbidden_runtime_ddl (id INTEGER);"));
                await ExpectDeniedAsync(() => ExecuteAsync(source, "UPDATE rpg_schema_migrations SET sha256 = repeat('0', 64);"));
                await ExpectDeniedAsync(() => PostgresSchemaMigrations.ApplyAsync(source,
                    [..catalog, new(catalog.Count + 1, "forbidden_delta", "CREATE TABLE forbidden_delta (id INTEGER);")], default));
                // The runtime is writable for gameplay, not a globally read-only connection.
                await ExecuteAsync(source, "UPDATE item_records SET quantity = quantity WHERE container_type = 'Inventory';");
            }
            string historyBefore = await HistorySnapshotAsync(fixture.Source);
            await RunHostAsync(runtime.ConnectionString);
            Require(await PlayerSnapshotAsync(fixture.Source) == playerBefore
                && await HistorySnapshotAsync(fixture.Source) == historyBefore,
                "Actual runtime host changed preserved rows or migration history.");
            Console.WriteLine("PASS Migration PG: non-owner restricted login starts real Production host/readiness without DDL or history writes");

            await ExecuteAsync(fixture.Source, "UPDATE rpg_schema_migrations SET sha256 = repeat('0', 64) WHERE version = 1;");
            await RunHostAsync(runtime.ConnectionString, "Migration history does not match this backend release.");
            await using (NpgsqlCommand restore = fixture.Source.CreateCommand(
                "UPDATE rpg_schema_migrations SET sha256 = @hash WHERE version = 1;"))
            {
                restore.Parameters.AddWithValue("hash", catalog[0].Sha256);
                await restore.ExecuteNonQueryAsync();
            }
            await PostgresSchemaMigrations.ApplyAsync(fixture.Source,
                [..catalog, new(catalog.Count + 1, "future_release", "SELECT 1;")], default);
            await RunHostAsync(runtime.ConnectionString, "The database schema is newer than this backend release.");
            Require(await PlayerSnapshotAsync(fixture.Source) == playerBefore,
                "Rejected runtime host changed preserved data.");
            Console.WriteLine("PASS Migration PG: actual Production startup rejects missing/changed/future history before listening");
        }
        finally
        {
            if (roleCreated)
            {
                Require(Regex.IsMatch(role, "\\Arpg_migration_runtime_[a-f0-9]{32}\\z"), "Unsafe role cleanup target.");
                // The generated role owns no objects; remove only its grants in
                // this private schema before dropping that exact role.
                await ExecuteAsync(fixture.Source, $"""
                    REVOKE ALL PRIVILEGES ON ALL TABLES IN SCHEMA "{fixture.Name}" FROM "{role}";
                    REVOKE ALL PRIVILEGES ON SCHEMA "{fixture.Name}" FROM "{role}";
                    DROP ROLE "{role}";
                    """);
                Console.WriteLine("Fixture role cleaned: " + role);
            }
        }
    }

    private static async Task RunHostAsync(string connectionString, string? expectedFailure = null)
    {
        ProcessStartInfo start = new("dotnet")
        {
            UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true, RedirectStandardError = true, WorkingDirectory = AppContext.BaseDirectory
        };
        start.ArgumentList.Add(typeof(PostgresSchemaMigrations).Assembly.Location);
        start.Environment["ASPNETCORE_ENVIRONMENT"] = "Production";
        start.Environment["DOTNET_ENVIRONMENT"] = "Production";
        start.Environment["POSTGRES_CONNECTION_STRING"] = connectionString;
        start.Environment["Storage__Provider"] = "Postgres";
        start.Environment["Steam__AllowDevelopmentAuthentication"] = "false";
        start.Environment["Kestrel__Endpoints__Http__Url"] = "http://127.0.0.1:0";
        start.Environment["Logging__LogLevel__Default"] = "Warning";
        start.Environment["Logging__LogLevel__Microsoft.Hosting.Lifetime"] = "Information";
        using Process child = new() { StartInfo = start };
        Require(child.Start(), "Could not start owned Production host.");
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(25));
        Task<string> errors = child.StandardError.ReadToEndAsync();
        try
        {
            if (expectedFailure is not null)
            {
                Task<string> output = child.StandardOutput.ReadToEndAsync();
                await child.WaitForExitAsync(deadline.Token);
                string combined = await output + await errors;
                Require(child.ExitCode != 0 && combined.Contains(expectedFailure, StringComparison.Ordinal)
                    && !combined.Contains("Now listening", StringComparison.Ordinal), "Production rejection did not precede hosting.");
                return;
            }
            StringBuilder log = new();
            Uri? address = null;
            while (await child.StandardOutput.ReadLineAsync(deadline.Token) is string line)
            {
                log.AppendLine(line);
                const string listening = "Now listening on: ";
                int at = line.IndexOf(listening, StringComparison.Ordinal);
                if (at >= 0) { address = new Uri(line[(at + listening.Length)..].Trim()); break; }
            }
            Require(address is not null && address.IsLoopback && address.Port != 0, "Production host did not reach its private listener.");
            Task<string> remaining = child.StandardOutput.ReadToEndAsync();
            using HttpClient http = new() { Timeout = TimeSpan.FromSeconds(5) };
            using HttpResponseMessage ready = await http.GetAsync(new Uri(address!, "/health/ready"), deadline.Token);
            Require(ready.IsSuccessStatusCode, "Restricted Production host readiness failed.");
            // We own this positive smoke host. Its forced teardown is not a
            // claim of natural web-host exit 0 or graceful drain validation.
            child.Kill(entireProcessTree: true);
            await child.WaitForExitAsync(deadline.Token);
            string finalLog = log + await remaining + await errors;
            Require(!finalLog.Contains("fail:", StringComparison.OrdinalIgnoreCase)
                && !finalLog.Contains("Unhandled exception", StringComparison.Ordinal), "Production worker/startup logged an error.");
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

    private static Task<string> PlayerSnapshotAsync(NpgsqlDataSource source) => ScalarAsync<string>(source, """
        SELECT jsonb_build_array(
            (SELECT jsonb_agg(to_jsonb(t) ORDER BY steam_id) FROM accounts t),
            (SELECT jsonb_agg(to_jsonb(t) ORDER BY roster_id) FROM rosters t),
            (SELECT jsonb_agg(to_jsonb(t) ORDER BY character_id) FROM characters t),
            (SELECT jsonb_agg(to_jsonb(t) ORDER BY token_hash) FROM auth_sessions t),
            (SELECT jsonb_agg(to_jsonb(t) ORDER BY item_id) FROM item_records t),
            (SELECT jsonb_agg(to_jsonb(t) ORDER BY currency_code) FROM currency_definitions t),
            (SELECT jsonb_agg(to_jsonb(t) ORDER BY owner_id, currency_code) FROM currency_balances t)
        )::text;
        """);
    private static Task<string> HistorySnapshotAsync(NpgsqlDataSource source) => ScalarAsync<string>(source,
        "SELECT jsonb_agg(to_jsonb(t) ORDER BY version)::text FROM rpg_schema_migrations t;");
    private static async Task ExecuteAsync(NpgsqlDataSource source, string sql)
    {
        await using NpgsqlCommand command = source.CreateCommand(sql);
        await command.ExecuteNonQueryAsync();
    }
    private static async Task ExpectDeniedAsync(Func<Task> body)
    {
        try { await body(); }
        catch (PostgresException exception) when (exception.SqlState == PostgresErrorCodes.InsufficientPrivilege) { return; }
        throw new TestFailure("Restricted role unexpectedly performed a privileged write.");
    }
    private static async Task<T> ScalarAsync<T>(NpgsqlDataSource source, string sql)
    {
        await using NpgsqlCommand command = source.CreateCommand(sql);
        object? result = await command.ExecuteScalarAsync();
        Require(result is T, "Unexpected SQL scalar result type.");
        return (T)result!;
    }
    private static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
