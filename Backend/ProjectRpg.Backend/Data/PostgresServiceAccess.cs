using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

internal static class PostgresServiceAccess
{
    // A fixed namespace, separate from account and request locks. Shared admissions
    // may proceed together; a transition waits for their entire transactions.
    internal static async Task LockAsync(NpgsqlConnection connection, NpgsqlTransaction tx, bool exclusive, CancellationToken ct)
    {
        await using var gate = new NpgsqlCommand(exclusive
            ? "SELECT pg_advisory_xact_lock(1852993381,1)" : "SELECT pg_advisory_xact_lock_shared(1852993381,1)", connection, tx);
        await gate.ExecuteNonQueryAsync(ct);
    }
    internal static async Task EnsureOpenAsync(NpgsqlConnection connection, NpgsqlTransaction tx, CancellationToken ct)
    {
        await LockAsync(connection, tx, false, ct);
        await using var query = new NpgsqlCommand("SELECT mode FROM ops_service_control WHERE id=1", connection, tx);
        if ((string?)await query.ExecuteScalarAsync(ct) != "Open") throw new ServiceDrainingException();
    }
}
