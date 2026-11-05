using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

internal static class PostgresAccountAccess
{
    // Shared by login/session issuance, every new admission boundary and operator restriction changes.
    internal static async Task LockAsync(NpgsqlConnection connection,NpgsqlTransaction transaction,string account,CancellationToken ct)
    {
        await using NpgsqlCommand gate=new("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))",connection,transaction);
        gate.Parameters.AddWithValue("key","ops_account:"+account); await gate.ExecuteNonQueryAsync(ct);
    }
    internal static async Task<bool> AllowAsync(NpgsqlConnection connection,NpgsqlTransaction transaction,string account,DateTimeOffset now,CancellationToken ct)
    {
        await LockAsync(connection,transaction,account,ct);
        await using NpgsqlCommand query=new("SELECT 1 FROM ops_account_restrictions WHERE account_id=@id AND state='Active' AND expires_at>@now",connection,transaction);
        query.Parameters.AddWithValue("id",account);query.Parameters.AddWithValue("now",now);
        return await query.ExecuteScalarAsync(ct) is null;
    }
    internal static async Task EnsureAsync(NpgsqlConnection connection,NpgsqlTransaction transaction,string account,DateTimeOffset now,CancellationToken ct)
    { if(!await AllowAsync(connection,transaction,account,now,ct))throw new AccountRestrictedException(); }
}
