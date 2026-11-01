using Npgsql;
using NpgsqlTypes;

namespace ProjectRpg.Backend.Data;

public static class PostgresSchemaInitializer
{
    // Database-wide, stable across processes and releases. The two-int key space
    // is separate from the bigint advisory keys used by item transactions.
    public const int LockNamespace = 0x52504753; // RPGS
    public const int LockResource = 0x5343484D; // SCHM

    public static async Task ApplyAsync(
        NpgsqlDataSource dataSource,
        string schema,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(dataSource);
        ArgumentException.ThrowIfNullOrWhiteSpace(schema);

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);

        // Acquire before any DDL or upgrade DML, on the same connection and
        // transaction as the entire trusted schema script. Command timeouts and
        // caller cancellation still apply while waiting for another initializer.
        await using (NpgsqlCommand schemaLock = new(
            "SELECT pg_advisory_xact_lock(@namespace_key, @resource_key);",
            connection,
            transaction))
        {
            schemaLock.Parameters.AddWithValue(
                "namespace_key", NpgsqlDbType.Integer, LockNamespace);
            schemaLock.Parameters.AddWithValue(
                "resource_key", NpgsqlDbType.Integer, LockResource);
            await schemaLock.ExecuteNonQueryAsync(cancellationToken);
        }

        await using (NpgsqlCommand command = new(schema, connection, transaction))
        {
            await command.ExecuteNonQueryAsync(cancellationToken);
        }

        await transaction.CommitAsync(cancellationToken);
        // Disposal rolls back on any failure/cancellation. A transaction-level
        // lock cannot remain attached to a pooled connection after rollback.
    }
}
