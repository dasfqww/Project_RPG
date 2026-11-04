using System.Data;
using Npgsql;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

// Connection lifetime and failure translation only; no domain writes or generic repository API.
internal sealed class PostgresReadSession(NpgsqlConnection connection, NpgsqlTransaction transaction) : IAsyncDisposable
{
    internal NpgsqlCommand Command(string sql) => new(sql, connection, transaction) { CommandTimeout = 3 };
    public async ValueTask DisposeAsync()
    {
        await transaction.DisposeAsync();
        await connection.DisposeAsync();
    }
    internal static Task<Result<T>> RunAsync<T>(NpgsqlDataSource source, OperationsStorageTrace trace,
        Func<PostgresReadSession, Task<Result<T>>> read, CancellationToken ct) where T : notnull =>
        trace.ObserveAsync(() => RunCoreAsync(source, read, ct));

    private static async Task<Result<T>> RunCoreAsync<T>(NpgsqlDataSource source,
        Func<PostgresReadSession, Task<Result<T>>> read, CancellationToken ct) where T : notnull
    {
        try
        {
            NpgsqlConnection connection = await source.OpenConnectionAsync(ct);
            NpgsqlTransaction transaction;
            try { transaction = await connection.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct); }
            catch { await connection.DisposeAsync(); throw; }
            await using PostgresReadSession session = new(connection, transaction);
            await using (NpgsqlCommand mode = session.Command("SET TRANSACTION READ ONLY;"))
                await mode.ExecuteNonQueryAsync(ct);
            return await read(session); // Disposing rolls back this read-only snapshot.
        }
        catch (NpgsqlException) { return Result<T>.Fail(Failures.StorageUnavailable); }
        catch (TimeoutException) { return Result<T>.Fail(Failures.StorageUnavailable); }
    }
    internal async Task<bool> CharacterExistsAsync(Guid id, CancellationToken ct)
    {
        await using NpgsqlCommand command = Command("SELECT 1 FROM characters WHERE character_id=@id;");
        command.Parameters.AddWithValue("id", id);
        return await command.ExecuteScalarAsync(ct) is not null;
    }
}
