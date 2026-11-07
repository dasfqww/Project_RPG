using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Npgsql;
using NpgsqlTypes;

namespace ProjectRpg.Backend.Data;

public sealed record PostgresSchemaMigration
{
    public int Version { get; }
    public string Name { get; }
    public string Sql { get; }
    public string Sha256 { get; }

    public PostgresSchemaMigration(int version, string name, string sql)
    {
        if (version < 1) throw new ArgumentOutOfRangeException(nameof(version));
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentException.ThrowIfNullOrWhiteSpace(sql);
        if (!Regex.IsMatch(name, "\\A[a-z][a-z0-9_]{0,63}\\z"))
            throw new ArgumentException("Migration names must be stable lowercase identifiers.", nameof(name));
        Version = version;
        Name = name;
        // Git/Windows newline conversion must not change the deployment identity.
        Sql = sql.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n');
        Sha256 = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(Sql)));
    }
}

public sealed record PostgresSchemaMigrationHistory(int Version, string Name, string Sha256);

/// <summary>
/// Deployment-only DDL; normal production startup calls ValidateAsync (SELECT only).
/// A release supports exactly its recorded catalog, not an untested rolling upgrade.
/// </summary>
public static class PostgresSchemaMigrations
{
    public static async Task<IReadOnlyList<PostgresSchemaMigration>> LoadCatalogAsync(
        string baseDirectory, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(baseDirectory);
        string sql = await File.ReadAllTextAsync(
            Path.Combine(baseDirectory, "Database", "schema.sql"), cancellationToken);
        // Freeze this baseline after first deployment. Append a new version and
        // separately packaged SQL file here; never silently replace version 1.
        string equipment = await File.ReadAllTextAsync(
            Path.Combine(baseDirectory, "Database", "Migrations", "0002_item_equipment_progression.sql"), cancellationToken);
        string narrative = await File.ReadAllTextAsync(
            Path.Combine(baseDirectory, "Database", "Migrations", "0003_character_narrative.sql"), cancellationToken);
        string operations = await File.ReadAllTextAsync(
            Path.Combine(baseDirectory, "Database", "Migrations", "0004_operations_sessions.sql"), cancellationToken);
        string evidence = await File.ReadAllTextAsync(
            Path.Combine(baseDirectory, "Database", "Migrations", "0005_operations_evidence.sql"), cancellationToken);
        string compensation = await File.ReadAllTextAsync(
            Path.Combine(baseDirectory, "Database", "Migrations", "0006_operations_compensation.sql"), cancellationToken);
        string restrictions = await File.ReadAllTextAsync(Path.Combine(baseDirectory,"Database","Migrations","0007_operations_restrictions.sql"),cancellationToken);
        string reservations = await File.ReadAllTextAsync(Path.Combine(baseDirectory,"Database","Migrations","0008_trade_reservations.sql"),cancellationToken);
        string service = await File.ReadAllTextAsync(Path.Combine(baseDirectory,"Database","Migrations","0009_operations_service_control.sql"),cancellationToken);
        string trading = await File.ReadAllTextAsync(Path.Combine(baseDirectory,"Database","Migrations","0010_trade_sessions.sql"),cancellationToken);
        string inventoryTabs = await File.ReadAllTextAsync(Path.Combine(baseDirectory,"Database","Migrations","0011_inventory_tabs.sql"),cancellationToken);
        return [new PostgresSchemaMigration(1, "baseline", sql),
            new PostgresSchemaMigration(2, "item_equipment_progression", equipment),
            new PostgresSchemaMigration(3, "character_narrative", narrative),
            new PostgresSchemaMigration(4, "operations_sessions", operations),
            new PostgresSchemaMigration(5, "operations_evidence", evidence),
            new PostgresSchemaMigration(6, "operations_compensation", compensation),
            new PostgresSchemaMigration(7, "operations_restrictions", restrictions),
            new PostgresSchemaMigration(8, "trade_reservations", reservations),
            new PostgresSchemaMigration(9, "operations_service_control", service),
            new PostgresSchemaMigration(10, "trade_sessions", trading),
            new PostgresSchemaMigration(11, "inventory_tabs", inventoryTabs)];
    }

    // Development/Testing replays the current idempotent catalog under the existing
    // schema lock, without manufacturing production deployment history.
    public static async Task<string> LoadInitializationSqlAsync(string baseDirectory, CancellationToken cancellationToken)
        => string.Join("\n", (await LoadCatalogAsync(baseDirectory, cancellationToken)).Select(migration => migration.Sql));

    // Pure planning is also used before opening a connection. Reject unsupported
    // future releases, edits to applied SQL, gaps and out-of-order history.
    public static IReadOnlyList<PostgresSchemaMigration> GetPending(
        IReadOnlyList<PostgresSchemaMigration> catalog,
        IReadOnlyList<PostgresSchemaMigrationHistory> history)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        ArgumentNullException.ThrowIfNull(history);
        if (catalog.Count == 0) throw new InvalidOperationException("The migration catalog is empty.");
        HashSet<string> names = new(StringComparer.Ordinal);
        for (int index = 0; index < catalog.Count; index++)
        {
            PostgresSchemaMigration migration = catalog[index]
                ?? throw new InvalidOperationException("The migration catalog contains a null entry.");
            if (migration.Version != index + 1 || !names.Add(migration.Name))
                throw new InvalidOperationException("Migration versions must be contiguous and names unique.");
        }
        if (history.Count > catalog.Count)
            throw new InvalidOperationException("The database schema is newer than this backend release.");
        for (int index = 0; index < history.Count; index++)
        {
            PostgresSchemaMigration expected = catalog[index];
            PostgresSchemaMigrationHistory actual = history[index]
                ?? throw new InvalidOperationException("Migration history contains a null entry.");
            if (actual.Version != expected.Version
                || !string.Equals(actual.Name, expected.Name, StringComparison.Ordinal)
                || !string.Equals(actual.Sha256, expected.Sha256, StringComparison.Ordinal))
                throw new InvalidOperationException("Migration history does not match this backend release.");
        }
        return catalog.Skip(history.Count).ToArray();
    }

    public static async Task<int> ApplyAsync(NpgsqlDataSource source,
        IReadOnlyList<PostgresSchemaMigration> catalog, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(source);
        // Immutable entries and a private list prevent caller mutation while awaiting SQL.
        PostgresSchemaMigration[] snapshot = GetPending(catalog, []).ToArray();
        await using NpgsqlConnection connection = await source.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await using (NpgsqlCommand command = new(
            "SELECT pg_advisory_xact_lock(@namespace_key, @resource_key);", connection, transaction))
        {
            command.Parameters.AddWithValue("namespace_key", NpgsqlDbType.Integer,
                PostgresSchemaInitializer.LockNamespace);
            command.Parameters.AddWithValue("resource_key", NpgsqlDbType.Integer,
                PostgresSchemaInitializer.LockResource);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }
        await using (NpgsqlCommand command = new("""
            CREATE TABLE IF NOT EXISTS rpg_schema_migrations (
                version INTEGER PRIMARY KEY CHECK (version > 0),
                name VARCHAR(64) NOT NULL UNIQUE,
                sha256 VARCHAR(64) NOT NULL CHECK (sha256 ~ '^[0-9a-f]{64}$'),
                applied_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            """, connection, transaction))
        {
            await command.ExecuteNonQueryAsync(cancellationToken);
        }
        IReadOnlyList<PostgresSchemaMigration> pending = GetPending(snapshot,
            await ReadHistoryAsync(connection, transaction, cancellationToken));
        foreach (PostgresSchemaMigration migration in pending)
        {
            await using (NpgsqlCommand command = new(migration.Sql, connection, transaction))
                await command.ExecuteNonQueryAsync(cancellationToken);
            await using NpgsqlCommand record = new("""
                INSERT INTO rpg_schema_migrations (version, name, sha256)
                VALUES (@version, @name, @sha256);
                """, connection, transaction);
            record.Parameters.AddWithValue("version", migration.Version);
            record.Parameters.AddWithValue("name", migration.Name);
            record.Parameters.AddWithValue("sha256", migration.Sha256);
            await record.ExecuteNonQueryAsync(cancellationToken);
        }
        // All pending SQL and history are atomic, including first-time adoption
        // of an existing unversioned schema. Do not stamp without applying SQL.
        await transaction.CommitAsync(cancellationToken);
        return pending.Count;
    }

    public static async Task ValidateAsync(NpgsqlDataSource source,
        IReadOnlyList<PostgresSchemaMigration> catalog, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(source);
        PostgresSchemaMigration[] snapshot = GetPending(catalog, []).ToArray();
        await using NpgsqlConnection connection = await source.OpenConnectionAsync(cancellationToken);
        IReadOnlyList<PostgresSchemaMigrationHistory> history;
        try
        {
            // One statement observes one committed history snapshot. No DDL,
            // advisory lock, automatic repair, stamping or history writes here.
            history = await ReadHistoryAsync(connection, null, cancellationToken);
        }
        catch (PostgresException exception) when (exception.SqlState == PostgresErrorCodes.UndefinedTable)
        {
            throw new InvalidOperationException(
                "Database migration history is missing. Run --migrate-schema with the deployment role first.",
                exception);
        }
        if (GetPending(snapshot, history).Count != 0)
            throw new InvalidOperationException(
                "Database migrations are pending. Run --migrate-schema before starting this release.");
    }

    private static async Task<IReadOnlyList<PostgresSchemaMigrationHistory>> ReadHistoryAsync(
        NpgsqlConnection connection, NpgsqlTransaction? transaction, CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new(
            "SELECT version, name, sha256 FROM rpg_schema_migrations ORDER BY version;",
            connection, transaction);
        await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
        List<PostgresSchemaMigrationHistory> history = [];
        while (await reader.ReadAsync(cancellationToken))
            history.Add(new(reader.GetInt32(0), reader.GetString(1), reader.GetString(2)));
        return history;
    }
}
