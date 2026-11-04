using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

public sealed class PostgresCharacterReadStore(NpgsqlDataSource source, OperationsStorageTrace trace) : ICharacterReadStore
{
    private const string Projection = """
        SELECT c.character_id,c.steam_id,c.roster_id,c.name,c.created_at,p.state::text
        FROM characters c LEFT JOIN character_progression p ON p.character_id=c.character_id
        """;
    public Task<Result<Page<CharacterHeader>>> SearchAsync(CharacterLookup query, CancellationToken ct) =>
        PostgresReadSession.RunAsync(source, trace, async session =>
        {
            string condition = query.Kind == CharacterLookupKind.SteamId ? "c.steam_id=@lookup" : "c.character_id=@lookup";
            await using NpgsqlCommand command = session.Command(Projection + " WHERE " + condition
                + " AND (@after_id IS NULL OR (c.created_at,c.character_id) > (@after_time,@after_id))"
                + " ORDER BY c.created_at,c.character_id LIMIT @limit;");
            command.Parameters.AddWithValue("lookup", query.Kind == CharacterLookupKind.SteamId ? query.Value : Guid.Parse(query.Value));
            command.Parameters.AddWithValue("after_id", NpgsqlDbType.Uuid, (object?)query.Page.After?.Id ?? DBNull.Value);
            command.Parameters.AddWithValue("after_time", NpgsqlDbType.TimestampTz, (object?)query.Page.After?.Timestamp ?? DBNull.Value);
            command.Parameters.AddWithValue("limit", query.Page.Limit + 1);
            List<CharacterHeader> rows = [];
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct)) rows.Add(Read(reader));
            return Result<Page<CharacterHeader>>.Success(ReadMapping.Page(rows, query.Page.Limit, c => new(c.CharacterId, c.CreatedAtUtc)));
        }, ct);
    public Task<Result<CharacterHeader>> FindAsync(Guid characterId, CancellationToken ct) =>
        PostgresReadSession.RunAsync(source, trace, async session =>
        {
            await using NpgsqlCommand command = session.Command(Projection + " WHERE c.character_id=@id;");
            command.Parameters.AddWithValue("id", characterId);
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(ct);
            return await reader.ReadAsync(ct) ? Result<CharacterHeader>.Success(Read(reader)) : Result<CharacterHeader>.Fail(Failures.CharacterMissing);
        }, ct);
    private static CharacterHeader Read(NpgsqlDataReader reader) => new(reader.GetGuid(0), reader.GetString(1),
        reader.GetGuid(2), reader.GetString(3), reader.GetFieldValue<DateTimeOffset>(4),
        ReadMapping.Progression(reader.GetGuid(0), reader.IsDBNull(5) ? null : reader.GetString(5)));
}
