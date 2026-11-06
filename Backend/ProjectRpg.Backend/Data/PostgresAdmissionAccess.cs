using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

internal static class PostgresAdmissionAccess
{
    // Composition owns the lock order. Account policy also serves non-admission
    // operator changes, so it must not acquire or know about the service gate.
    internal static async Task<bool> AllowAsync(NpgsqlConnection connection, NpgsqlTransaction tx, string account, DateTimeOffset now, CancellationToken ct)
    {
        await PostgresServiceAccess.EnsureOpenAsync(connection, tx, ct);
        return await PostgresAccountAccess.AllowAsync(connection, tx, account, now, ct);
    }
    internal static async Task EnsureAsync(NpgsqlConnection connection, NpgsqlTransaction tx, string account, DateTimeOffset now, CancellationToken ct)
    { if (!await AllowAsync(connection, tx, account, now, ct)) throw new AccountRestrictedException(); }
}
